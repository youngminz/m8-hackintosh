#!/usr/bin/env python3
# ffmpeg의 trace_headers 출력과 vp9dump -t 출력을 프레임마다, 구문 요소마다 견준다.
#
#   ffmpeg -v verbose -i X.ivf -c copy -bsf:v trace_headers -f null - 2> trace.txt
#   ./vp9dump X.ivf -t > dump.txt
#   python3 compare_trace.py trace.txt dump.txt
#
# trace 쪽은 ffmpeg CBS(cbs_vp9_syntax_template.c)가 찍은 "비트위치 이름 비트열 = 값" 줄을 프레임별 사전으로 바꾼다.
# 참조 슬롯의 크기 물려받기(found_ref)와 비압축 헤더 크기(header_size_in_bytes의 비트 위치에서 계산)는
# 여기서 따로 계산해 vp9dump와 독립적으로 맞는지 본다.
import re, sys

def parse_trace(path):
    frames, cur, skip = [], None, False
    for line in open(path, errors='replace'):
        line = re.sub(r'^\[trace_headers @ [0-9a-fx]+\] ', '', line.rstrip('\n'))
        if line.startswith('Packet:'):
            continue
        if re.fullmatch(r'[A-Z][A-Za-z ]*', line.strip()):  # 구역 제목: "Frame", "Superframe Index" 등
            skip = line.strip() != 'Frame'
            if not skip:
                cur = []
                frames.append(cur)
            continue
        m = re.match(r'^(\d+)\s+(\S+)\s+([01]+) = (-?\d+)$', line)
        if m and cur is not None and not skip:
            cur.append((int(m.group(1)), m.group(2), int(m.group(4))))
    return frames

def canon(elems, slots):
    e = {}
    pos = {}
    for p, name, v in elems:
        if name in ('zero_bit', 'reserved_zero'):
            continue
        e[name] = v
        pos[name] = p
    g = lambda k, d=0: e.get(k, d)
    profile = g('profile_low_bit') + 2 * g('profile_high_bit')
    d = {'profile': profile, 'show_existing_frame': g('show_existing_frame')}
    if d['show_existing_frame']:
        d['frame_to_show_map_idx'] = g('frame_to_show_map_idx')
        return d, None
    key = g('frame_type') == 0
    intra_only = g('intra_only')
    for k in ('frame_type', 'show_frame', 'error_resilient_mode', 'intra_only', 'reset_frame_context'):
        d[k] = g(k)
    d['refresh_frame_flags'] = 255 if key else g('refresh_frame_flags')
    if 'color_space' in e:
        d['bit_depth'] = (12 if e['ten_or_twelve_bit'] else 10) if 'ten_or_twelve_bit' in e else 8
        d['color_space'] = e['color_space']
        if 'color_range' in e:
            d['color_range'] = e['color_range']
        if profile in (1, 3):
            d['subsampling_x'] = g('subsampling_x'); d['subsampling_y'] = g('subsampling_y')
    if not key and not intra_only:
        found = 0
        for i in range(3):
            d['ref_frame_idx%d' % i] = e['ref_frame_idx[%d]' % i]
            d['sign_bias%d' % i] = e['ref_frame_sign_bias[%d]' % (i + 1)]
            if g('found_ref[%d]' % i):
                found = i + 1
        d['found_ref'] = found
        d['allow_high_precision_mv'] = g('allow_high_precision_mv')
        d['is_filter_switchable'] = g('is_filter_switchable')
        if not d['is_filter_switchable']:
            d['raw_interpolation_filter'] = g('raw_interpolation_filter_type')
        if found:
            w, h = slots[e['ref_frame_idx[%d]' % (found - 1)]]
    if 'frame_width_minus_1' in e:
        w, h = e['frame_width_minus_1'] + 1, e['frame_height_minus_1'] + 1
    d['frame_width'], d['frame_height'] = w, h
    d['render_and_frame_size_different'] = g('render_and_frame_size_different')
    if d['render_and_frame_size_different']:
        d['render_width'] = e['render_width_minus_1'] + 1; d['render_height'] = e['render_height_minus_1'] + 1
    err = d['error_resilient_mode']
    d['refresh_frame_context'] = 0 if err else g('refresh_frame_context')
    d['frame_parallel_decoding_mode'] = 1 if err else g('frame_parallel_decoding_mode')
    for k in ('frame_context_idx', 'loop_filter_level', 'loop_filter_sharpness', 'loop_filter_delta_enabled', 'loop_filter_delta_update', 'base_q_idx'):
        d[k] = g(k)
    for i in range(4):
        d['update_ref_delta%d' % i] = g('update_ref_delta[%d]' % i)
        if d['update_ref_delta%d' % i]:
            d['ref_delta%d' % i] = e['loop_filter_ref_deltas[%d]' % i]
    for i in range(2):
        d['update_mode_delta%d' % i] = g('update_mode_delta[%d]' % i)
        if d['update_mode_delta%d' % i]:
            d['mode_delta%d' % i] = e['loop_filter_mode_deltas[%d]' % i]
    for k in ('delta_q_y_dc', 'delta_q_uv_dc', 'delta_q_uv_ac'):
        d[k] = e[k + '.delta_q'] if g(k + '.delta_coded') else 0
    for k in ('segmentation_enabled', 'segmentation_update_map', 'segmentation_temporal_update', 'segmentation_update_data'):
        d[k] = g(k)
    if d['segmentation_update_map']:
        for i in range(7):
            n = 'segmentation_tree_probs[%d]' % i
            d['tree_prob%d' % i] = e[n + '.prob'] if g(n + '.prob_coded') else 255
        for i in range(3):
            n = 'segmentation_pred_prob[%d]' % i
            d['pred_prob%d' % i] = e[n + '.prob'] if g(n + '.prob_coded') else 255
    if d['segmentation_update_data']:
        d['segmentation_abs_or_delta_update'] = g('segmentation_abs_or_delta_update')
        for i in range(8):
            for j in range(4):
                en = g('feature_enabled[%d][%d]' % (i, j))
                d['feature_enabled%d_%d' % (i, j)] = en
                if en and j < 3:
                    v = e['feature_value[%d][%d]' % (i, j)]
                    d['feature_data%d_%d' % (i, j)] = -v if g('feature_sign[%d][%d]' % (i, j)) else v
    d['tile_cols_log2'] = g('tile_cols_log2'); d['tile_rows_log2'] = g('tile_rows_log2')
    d['header_size_in_bytes'] = e['header_size_in_bytes']
    d['uncompressed_header_size'] = (pos['header_size_in_bytes'] + 16 + 7) // 8
    return d, (d['refresh_frame_flags'], (w, h))

def parse_dump(path):
    frames = []
    for line in open(path):
        m = re.match(r'^frame (\d+) packet (\d+) size (\d+):(.*)$', line)
        if m:
            frames.append({k: int(v) for k, v in (t.split('=') for t in m.group(4).split())})
        elif 'PARSE ERROR' in line:
            frames.append({'PARSE_ERROR': 1})
    return frames

trace, dump = parse_trace(sys.argv[1]), parse_dump(sys.argv[2])
slots, bad, fields, seen = {}, 0, 0, set()
for i, elems in enumerate(trace):
    d, upd = canon(elems, slots)
    if upd:
        for s in range(8):
            if upd[0] >> s & 1:
                slots[s] = upd[1]
    if i >= len(dump):
        break
    fields += len(d); seen |= set(d)
    if d != dump[i]:
        bad += 1
        diff = {k: (d.get(k), dump[i].get(k)) for k in sorted(set(d) | set(dump[i])) if d.get(k) != dump[i].get(k)}
        print('frame %d MISMATCH (trace, vp9dump): %s' % (i, diff))
print('trace frames %d, vp9dump frames %d, compared fields %d (%d distinct names), mismatching frames %d' % (len(trace), len(dump), fields, len(seen), bad))
sys.exit(1 if bad or len(trace) != len(dump) else 0)
