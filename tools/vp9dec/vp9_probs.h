/**************************************************************************
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

// VCN VP9 디코더의 문맥 버퍼(hw context, RDECODE_CMD_CONTEXT_BUFFER)와 확률 버퍼(RDECODE_CMD_PROB_TBL_BUFFER)
// 초기화에 쓰는 VP9 기본 확률 표와 초기화 함수.
//
// 출처: Mesa 태그 mesa-25.0.0 (커밋 4fa244fddfebb21378042556862e197284ef65ac)
//   src/gallium/auxiliary/vl/vl_probs_table.h         표 전부(아래 "vl_probs_table.h에서" 구역, 원본 그대로)
//   src/gallium/drivers/radeonsi/radeon_vcn_dec.c     fill_probs_table()과 ctx 크기 계산(맨 아래 구역)
// Mesa 25.0.0에는 rvcn_init_mode_probs 같은 VP9용 함수가 따로 없다(그 이름은 AV1용 ac_vcn_av1_init_*이다).
// VP9는 fill_probs_table() 하나가 rvcn_dec_vp9_probs_t 구조체에 표를 memcpy하는 것이 전부다.
// 위 저작권 고지는 vl_probs_table.h 원본의 것이다(MIT 계열). radeon_vcn_dec.c는
// "Copyright 2017 Advanced Micro Devices, Inc. SPDX-License-Identifier: MIT"이다.

#ifndef VP9_PROBS_H
#define VP9_PROBS_H

#include <string.h>
#include "vcn_vp9.h"

/* ===== vl_probs_table.h에서 ===== */

static const unsigned char default_coef_probs_4x4[2][2][6][6][3] = {
   {  /* Y plane */
      {  /* Intra */
         {  /* Band 0 */
            { 195,  29, 183 }, {  84,  49, 136 }, {   8,  42,  71 }
         },
         {  /* Band 1 */
           {  31, 107, 169 }, {  35,  99, 159 }, {  17,  82, 140 },
           {   8,  66, 114 }, {   2,  44,  76 }, {   1,  19,  32 }
         },
         {  /* Band 2 */
           {  40, 132, 201 }, {  29, 114, 187 }, {  13,  91, 157 },
           {   7,  75, 127 }, {   3,  58,  95 }, {   1,  28,  47 }
         },
         {  /* Band 3 */
           {  69, 142, 221 }, {  42, 122, 201 }, {  15,  91, 159 },
           {   6,  67, 121 }, {   1,  42,  77 }, {   1,  17,  31 }
         },
         {  /* Band 4 */
           { 102, 148, 228 }, {  67, 117, 204 }, {  17,  82, 154 },
           {   6,  59, 114 }, {   2,  39,  75 }, {   1,  15,  29 }
         },
         {  /* Band 5 */
           { 156,  57, 233 }, { 119,  57, 212 }, {  58,  48, 163 },
           {  29,  40, 124 }, {  12,  30,  81 }, {   3,  12,  31 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
           { 191, 107, 226 }, { 124, 117, 204 }, {  25,  99, 155 }
         },
         {  /* Band 1 */
           {  29, 148, 210 }, {  37, 126, 194 }, {   8,  93, 157 },
           {   2,  68, 118 }, {   1,  39,  69 }, {   1,  17,  33 }
         },
         {  /* Band 2 */
           {  41, 151, 213 }, {  27, 123, 193 }, {   3,  82, 144 },
           {   1,  58, 105 }, {   1,  32,  60 }, {   1,  13,  26 }
         },
         {  /* Band 3 */
           {  59, 159, 220 }, {  23, 126, 198 }, {   4,  88, 151 },
           {   1,  66, 114 }, {   1,  38,  71 }, {   1,  18,  34 }
         },
         {  /* Band 4 */
           { 114, 136, 232 }, {  51, 114, 207 }, {  11,  83, 155 },
           {   3,  56, 105 }, {   1,  33,  65 }, {   1,  17,  34 }
         },
         {  /* Band 5 */
           { 149,  65, 234 }, { 121,  57, 215 }, {  61,  49, 166 },
           {  28,  36, 114 }, {  12,  25,  76 }, {   3,  16,  42 }
         }
      }
   },
   {  /* UV plane */
      {  /* Intra */
         {  /* Band 0 */
            { 214,  49, 220 }, { 132,  63, 188 }, {  42,  65, 137 }
         },
         {  /* Band 1 */
            {  85, 137, 221 }, { 104, 131, 216 }, {  49, 111, 192 },
            {  21,  87, 155 }, {   2,  49,  87 }, {   1,  16,  28 }
         },
         {  /* Band 2 */
            {  89, 163, 230 }, {  90, 137, 220 }, {  29, 100, 183 },
            {  10,  70, 135 }, {   2,  42,  81 }, {   1,  17,  33 }
         },
         {  /* Band 3 */
            { 108, 167, 237 }, {  55, 133, 222 }, {  15,  97, 179 },
            {   4,  72, 135 }, {   1,  45,  85 }, {   1,  19,  38 }
         },
         {  /* Band 4 */
            { 124, 146, 240 }, {  66, 124, 224 }, {  17,  88, 175 },
            {   4,  58, 122 }, {   1,  36,  75 }, {   1,  18,  37 }
         },
         {  /*  Band 5 */
            { 141,  79, 241 }, { 126,  70, 227 }, {  66,  58, 182 },
            {  30,  44, 136 }, {  12,  34,  96 }, {   2,  20,  47 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
            { 229,  99, 249 }, { 143, 111, 235 }, {  46, 109, 192 }
         },
         {  /* Band 1 */
            {  82, 158, 236 }, {  94, 146, 224 }, {  25, 117, 191 },
            {   9,  87, 149 }, {   3,  56,  99 }, {   1,  33,  57 }
         },
         {  /* Band 2 */
            {  83, 167, 237 }, {  68, 145, 222 }, {  10, 103, 177 },
            {   2,  72, 131 }, {   1,  41,  79 }, {   1,  20,  39 }
         },
         {  /* Band 3 */
            {  99, 167, 239 }, {  47, 141, 224 }, {  10, 104, 178 },
            {   2,  73, 133 }, {   1,  44,  85 }, {   1,  22,  47 }
         },
         {  /* Band 4 */
            { 127, 145, 243 }, {  71, 129, 228 }, {  17,  93, 177 },
            {   3,  61, 124 }, {   1,  41,  84 }, {   1,  21,  52 }
         },
         {  /* Band 5 */
            { 157,  78, 244 }, { 140,  72, 231 }, {  69,  58, 184 },
            {  31,  44, 137 }, {  14,  38, 105 }, {   8,  23,  61 }
         }
      }
   }
};

static const unsigned char default_coef_probs_8x8[2][2][6][6][3] = {
   {  /* Y plane */
      {  /* Intra */
         {  /* Band 0 */
            { 125,  34, 187 }, {  52,  41, 133 }, {   6,  31,  56 }
         },
         {  /* Band 1 */
            {  37, 109, 153 }, {  51, 102, 147 }, {  23,  87, 128 },
            {   8,  67, 101 }, {   1,  41,  63 }, {   1,  19,  29 }
         },
         {  /* Band 2 */
            {  31, 154, 185 }, {  17, 127, 175 }, {   6,  96, 145 },
            {   2,  73, 114 }, {   1,  51,  82 }, {   1,  28,  45 }
         },
         {  /* Band 3 */
            {  23, 163, 200 }, {  10, 131, 185 }, {   2,  93, 148 },
            {   1,  67, 111 }, {   1,  41,  69 }, {   1,  14,  24 }
         },
         {  /* Band 4 */
            {  29, 176, 217 }, {  12, 145, 201 }, {   3, 101, 156 },
            {   1,  69, 111 }, {   1,  39,  63 }, {   1,  14,  23 }
         },
         {  /* Band 5 */
            {  57, 192, 233 }, {  25, 154, 215 }, {   6, 109, 167 },
            {   3,  78, 118 }, {   1,  48,  69 }, {   1,  21,  29 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
            { 202, 105, 245 }, { 108, 106, 216 }, {  18,  90, 144 }
         },
         {  /* Band 1 */
            {  33, 172, 219 }, {  64, 149, 206 }, {  14, 117, 177 },
            {   5,  90, 141 }, {   2,  61,  95 }, {   1,  37,  57 }
         },
         {  /* Band 2 */
            {  33, 179, 220 }, {  11, 140, 198 }, {   1,  89, 148 },
            {   1,  60, 104 }, {   1,  33,  57 }, {   1,  12,  21 }
         },
         {  /* Band 3 */
            {  30, 181, 221 }, {   8, 141, 198 }, {   1,  87, 145 },
            {   1,  58, 100 }, {   1,  31,  55 }, {   1,  12,  20 }
         },
         {  /* Band 4 */
            {  32, 186, 224 }, {   7, 142, 198 }, {   1,  86, 143 },
            {   1,  58, 100 }, {   1,  31,  55 }, {   1,  12,  22 }
         },
         {  /* Band 5 */
            {  57, 192, 227 }, {  20, 143, 204 }, {   3,  96, 154 },
            {   1,  68, 112 }, {   1,  42,  69 }, {   1,  19,  32 }
         }
      }
   },
   {  /* UV plane */
      {  /* Intra */
         {  /* Band 0 */
            { 212,  35, 215 }, { 113,  47, 169 }, {  29,  48, 105 }
         },
         {  /* Band 1 */
            {  74, 129, 203 }, { 106, 120, 203 }, {  49, 107, 178 },
            {  19,  84, 144 }, {   4,  50,  84 }, {   1,  15,  25 }
         },
         {  /* Band 2 */
            {  71, 172, 217 }, {  44, 141, 209 }, {  15, 102, 173 },
            {   6,  76, 133 }, {   2,  51,  89 }, {   1,  24,  42 }
         },
         {  /* Band 3 */
            {  64, 185, 231 }, {  31, 148, 216 }, {   8, 103, 175 },
            {   3,  74, 131 }, {   1,  46,  81 }, {   1,  18,  30 }
         },
         {  /* Band 4 */
            {  65, 196, 235 }, {  25, 157, 221 }, {   5, 105, 174 },
            {   1,  67, 120 }, {   1,  38,  69 }, {   1,  15,  30 }
         },
         {  /* Band 5 */
            {  65, 204, 238 }, {  30, 156, 224 }, {   7, 107, 177 },
            {   2,  70, 124 }, {   1,  42,  73 }, {   1,  18,  34 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
            { 225,  86, 251 }, { 144, 104, 235 }, {  42,  99, 181 }
         },
         {  /* Band 1 */
            {  85, 175, 239 }, { 112, 165, 229 }, {  29, 136, 200 },
            {  12, 103, 162 }, {   6,  77, 123 }, {   2,  53,  84 }
         },
         {  /* Band 2 */
            {  75, 183, 239 }, {  30, 155, 221 }, {   3, 106, 171 },
            {   1,  74, 128 }, {   1,  44,  76 }, {   1,  17,  28 }
         },
         {  /* Band 3 */
            {  73, 185, 240 }, {  27, 159, 222 }, {   2, 107, 172 },
            {   1,  75, 127 }, {   1,  42,  73 }, {   1,  17,  29 }
         },
         {  /* Band 4 */
            {  62, 190, 238 }, {  21, 159, 222 }, {   2, 107, 172 },
            {   1,  72, 122 }, {   1,  40,  71 }, {   1,  18,  32 }
         },
         {  /* Band 5 */
            {  61, 199, 240 }, {  27, 161, 226 }, {   4, 113, 180 },
            {   1,  76, 129 }, {   1,  46,  80 }, {   1,  23,  41 }
         }
      }
   }
};

static const unsigned char default_coef_probs_16x16[2][2][6][6][3] = {
   {  /* Y plane */
      {  /* Intra */
         {  /* Band 0 */
            {   7,  27, 153 }, {   5,  30,  95 }, {   1,  16,  30 }
         },
         {  /* Band 1 */
            {  50,  75, 127 }, {  57,  75, 124 }, {  27,  67, 108 },
            {  10,  54,  86 }, {   1,  33,  52 }, {   1,  12,  18 }
         },
         {  /* Band 2 */
            {  43, 125, 151 }, {  26, 108, 148 }, {   7,  83, 122 },
            {   2,  59,  89 }, {   1,  38,  60 }, {   1,  17,  27 }
         },
         {  /* Band 3 */
            {  23, 144, 163 }, {  13, 112, 154 }, {   2,  75, 117 },
            {   1,  50,  81 }, {   1,  31,  51 }, {   1,  14,  23 }
         },
         {  /* Band 4 */
            {  18, 162, 185 }, {   6, 123, 171 }, {   1,  78, 125 },
            {   1,  51,  86 }, {   1,  31,  54 }, {   1,  14,  23 }
         },
         {  /* Band 5 */
            {  15, 199, 227 }, {   3, 150, 204 }, {   1,  91, 146 },
            {   1,  55,  95 }, {   1,  30,  53 }, {   1,  11,  20 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
            {  19,  55, 240 }, {  19,  59, 196 }, {   3,  52, 105 }
         },
         {  /* Band 1 */
            {  41, 166, 207 }, { 104, 153, 199 }, {  31, 123, 181 },
            {  14, 101, 152 }, {   5,  72, 106 }, {   1,  36,  52 }
         },
         {  /* Band 2 */
            {  35, 176, 211 }, {  12, 131, 190 }, {   2,  88, 144 },
            {   1,  60, 101 }, {   1,  36,  60 }, {   1,  16,  28 }
         },
         {  /* Band 3 */
            {  28, 183, 213 }, {   8, 134, 191 }, {   1,  86, 142 },
            {   1,  56,  96 }, {   1,  30,  53 }, {   1,  12,  20 }
         },
         {  /* Band 4 */
            {  20, 190, 215 }, {   4, 135, 192 }, {   1,  84, 139 },
            {   1,  53,  91 }, {   1,  28,  49 }, {   1,  11,  20 }
         },
         {  /* Band 5 */
            {  13, 196, 216 }, {   2, 137, 192 }, {   1,  86, 143 },
            {   1,  57,  99 }, {   1,  32,  56 }, {   1,  13,  24 }
         }
      }
   },
   {  /* UV plane */
      {  /* Intra */
         {  /* Band 0 */
            { 211,  29, 217 }, {  96,  47, 156 }, {  22,  43,  87 }
         },
         {  /* Band 1 */
            {  78, 120, 193 }, { 111, 116, 186 }, {  46, 102, 164 },
            {  15,  80, 128 }, {   2,  49,  76 }, {   1,  18,  28 }
         },
         {  /* Band 2 */
            {  71, 161, 203 }, {  42, 132, 192 }, {  10,  98, 150 },
            {   3,  69, 109 }, {   1,  44,  70 }, {   1,  18,  29 }
         },
         {  /* Band 3 */
            {  57, 186, 211 }, {  30, 140, 196 }, {   4,  93, 146 },
            {   1,  62, 102 }, {   1,  38,  65 }, {   1,  16,  27 }
         },
         {  /* Band 4 */
            {  47, 199, 217 }, {  14, 145, 196 }, {   1,  88, 142 },
            {   1,  57,  98 }, {   1,  36,  62 }, {   1,  15,  26 }
         },
         {  /* Band 5 */
            {  26, 219, 229 }, {   5, 155, 207 }, {   1,  94, 151 },
            {   1,  60, 104 }, {   1,  36,  62 }, {   1,  16,  28 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
            { 233,  29, 248 }, { 146,  47, 220 }, {  43,  52, 140 }
         },
         {  /* Band 1 */
            { 100, 163, 232 }, { 179, 161, 222 }, {  63, 142, 204 },
            {  37, 113, 174 }, {  26,  89, 137 }, {  18,  68,  97 }
         },
         {  /* Band 2 */
            {  85, 181, 230 }, {  32, 146, 209 }, {   7, 100, 164 },
            {   3,  71, 121 }, {   1,  45,  77 }, {   1,  18,  30 }
         },
         {  /* Band 3 */
            {  65, 187, 230 }, {  20, 148, 207 }, {   2,  97, 159 },
            {   1,  68, 116 }, {   1,  40,  70 }, {   1,  14,  29 }
         },
         {  /* Band 4 */
            {  40, 194, 227 }, {   8, 147, 204 }, {   1,  94, 155 },
            {   1,  65, 112 }, {   1,  39,  66 }, {   1,  14,  26 }
         },
         {  /* Band 5 */
            {  16, 208, 228 }, {   3, 151, 207 }, {   1,  98, 160 },
            {   1,  67, 117 }, {   1,  41,  74 }, {   1,  17,  31 }
         }
      }
   }
};

static const unsigned char default_coef_probs_32x32[2][2][6][6][3] = {
   {  /* Y plane */
      {  /* Intra */
         {  /* Band 0 */
            {  17,  38, 140 }, {   7,  34,  80 }, {   1,  17,  29 }
         },
         {  /* Band 1 */
            {  37,  75, 128 }, {  41,  76, 128 }, {  26,  66, 116 },
            {  12,  52,  94 }, {   2,  32,  55 }, {   1,  10,  16 }
         },
         {  /* Band 2 */
            {  50, 127, 154 }, {  37, 109, 152 }, {  16,  82, 121 },
            {   5,  59,  85 }, {   1,  35,  54 }, {   1,  13,  20 }
         },
         {  /* Band 3 */
            {  40, 142, 167 }, {  17, 110, 157 }, {   2,  71, 112 },
            {   1,  44,  72 }, {   1,  27,  45 }, {   1,  11,  17 }
         },
         {  /* Band 4 */
            {  30, 175, 188 }, {   9, 124, 169 }, {   1,  74, 116 },
            {   1,  48,  78 }, {   1,  30,  49 }, {   1,  11,  18 }
         },
         {  /* Band 5 */
            {  10, 222, 223 }, {   2, 150, 194 }, {   1,  83, 128 },
            {   1,  48,  79 }, {   1,  27,  45 }, {   1,  11,  17 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
            {  36,  41, 235 }, {  29,  36, 193 }, {  10,  27, 111 }
         },
         {  /* Band 1 */
            {  85, 165, 222 }, { 177, 162, 215 }, { 110, 135, 195 },
            {  57, 113, 168 }, {  23,  83, 120 }, {  10,  49,  61 }
         },
         {  /* Band 2 */
            {  85, 190, 223 }, {  36, 139, 200 }, {   5,  90, 146 },
            {   1,  60, 103 }, {   1,  38,  65 }, {   1,  18,  30 }
         },
         {  /* Band 3 */
            {  72, 202, 223 }, {  23, 141, 199 }, {   2,  86, 140 },
            {   1,  56,  97 }, {   1,  36,  61 }, {   1,  16,  27 }
         },
         {  /* Band 4 */
            {  55, 218, 225 }, {  13, 145, 200 }, {   1,  86, 141 },
            {   1,  57,  99 }, {   1,  35,  61 }, {   1,  13,  22 }
         },
         {  /* Band 5 */
            {  15, 235, 212 }, {   1, 132, 184 }, {   1,  84, 139 },
            {   1,  57,  97 }, {   1,  34,  56 }, {   1,  14,  23 }
         }
      }
   },
   {  /* UV plane */
      {  /* Intra */
         {  /* Band 0 */
            { 181,  21, 201 }, {  61,  37, 123 }, {  10,  38,  71 }
         },
         {  /* Band 1 */
            {  47, 106, 172 }, {  95, 104, 173 }, {  42,  93, 159 },
            {  18,  77, 131 }, {   4,  50,  81 }, {   1,  17,  23 }
         },
         {  /* Band 2 */
            {  62, 147, 199 }, {  44, 130, 189 }, {  28, 102, 154 },
            {  18,  75, 115 }, {   2,  44,  65 }, {   1,  12,  19 }
         },
         {  /* Band 3 */
            {  55, 153, 210 }, {  24, 130, 194 }, {   3,  93, 146 },
            {   1,  61,  97 }, {   1,  31,  50 }, {   1,  10,  16 }
         },
         {  /* Band 4 */
            {  49, 186, 223 }, {  17, 148, 204 }, {   1,  96, 142 },
            {   1,  53,  83 }, {   1,  26,  44 }, {   1,  11,  17 }
         },
         {  /* Band 5 */
            {  13, 217, 212 }, {   2, 136, 180 }, {   1,  78, 124 },
            {   1,  50,  83 }, {   1,  29,  49 }, {   1,  14,  23 }
         }
      },
      {  /* Inter */
         {  /* Band 0 */
            { 197,  13, 247 }, {  82,  17, 222 }, {  25,  17, 162 }
         },
         {  /* Band 1 */
            { 126, 186, 247 }, { 234, 191, 243 }, { 176, 177, 234 },
            { 104, 158, 220 }, {  66, 128, 186 }, {  55,  90, 137 }
         },
         {  /* Band 2 */
            { 111, 197, 242 }, {  46, 158, 219 }, {   9, 104, 171 },
            {   2,  65, 125 }, {   1,  44,  80 }, {   1,  17,  91 }
         },
         {  /* Band 3 */
            { 104, 208, 245 }, {  39, 168, 224 }, {   3, 109, 162 },
            {   1,  79, 124 }, {   1,  50, 102 }, {   1,  43, 102 }
         },
         {  /* Band 4 */
            {  84, 220, 246 }, {  31, 177, 231 }, {   2, 115, 180 },
            {   1,  79, 134 }, {   1,  55,  77 }, {   1,  60,  79 }
         },
         {  /* Band 5 */
            {  43, 243, 240 }, {   8, 180, 217 }, {   1, 115, 166 },
            {   1,  84, 121 }, {   1,  51,  67 }, {   1,  16,   6 }
         }
      }
   }
};

static const unsigned char default_if_y_probs[4][9] = {
   {  65,  32,  18, 144, 162, 194,  41,  51,  98 },  /* block_size < 8x8 */
   { 132,  68,  18, 165, 217, 196,  45,  40,  78 },  /* block_size < 16x16 */
   { 173,  80,  19, 176, 240, 193,  64,  35,  46 },  /* block_size < 32x32 */
   { 221, 135,  38, 194, 248, 121,  96,  85,  29 }   /* block_size >= 32x32 */
};

static const unsigned char default_if_uv_probs[10][9] = {
   { 120,   7,  76, 176, 208, 126,  28,  54, 103 },  /* y = dc */
   {  48,  12, 154, 155, 139,  90,  34, 117, 119 },  /* y = v */
   {  67,   6,  25, 204, 243, 158,  13,  21,  96 },  /* y = h */
   {  97,   5,  44, 131, 176, 139,  48,  68,  97 },  /* y = d45 */
   {  83,   5,  42, 156, 111, 152,  26,  49, 152 },  /* y = d135 */
   {  80,   5,  58, 178,  74,  83,  33,  62, 145 },  /* y = d117 */
   {  86,   5,  32, 154, 192, 168,  14,  22, 163 },  /* y = d153 */
   {  85,   5,  32, 156, 216, 148,  19,  29,  73 },  /* y = d207 */
   {  77,   7,  64, 116, 132, 122,  37, 126, 120 },  /* y = d63 */
   { 101,  21, 107, 181, 192, 103,  19,  67, 125 }   /* y = tm */
};

static const unsigned char default_single_ref_p[5][2] = {
   {  33,  16 },
   {  77,  74 },
   { 142, 142 },
   { 172, 170 },
   { 238, 247 }
};

static const unsigned char  default_switchable_interp_prob[4][2] = {
   { 235, 162, },
   { 36, 255, },
   { 34, 3, },
   { 149, 144, },
};

static const unsigned char default_partition_probs[16][3] = {
   /* 8x8 -> 4x4 */
   { 199, 122, 141 },  /* a/l both not split */
   { 147,  63, 159 },  /* a split, l not split */
   { 148, 133, 118 },  /* l split, a not split */
   { 121, 104, 114 },  /* a/l both split */
   /* 16x16 -> 8x8 */
   { 174,  73,  87 },  /* a/l both not split */
   {  92,  41,  83 },  /* a split, l not split */
   {  82,  99,  50 },  /* l split, a not split */
   {  53,  39,  39 },  /* a/l both split */
   /* 32x32 -> 16x16 */
   { 177,  58,  59 },  /* a/l both not split */
   {  68,  26,  63 },  /* a split, l not split */
   {  52,  79,  25 },  /* l split, a not split */
   {  17,  14,  12 },  /* a/l both split */
   /* 64x64 -> 32x32 */
   { 222,  34,  30 },  /* a/l both not split */
   {  72,  16,  44 },  /* a split, l not split */
   {  58,  32,  12 },  /* l split, a not split */
   {  10,   7,   6 },  /* a/l both split */
};

static const unsigned char default_inter_mode_probs[7][3] = {
   {2,       173,   34},  /* 0 = both zero mv */
   {7,       145,   85},  /* 1 = one zero mv + one a predicted mv */
   {7,       166,   63},  /* 2 = two predicted mvs */
   {7,       94,    66},  /* 3 = one predicted/zero and one new mv */
   {8,       64,    46},  /* 4 = two new mvs */
   {17,      81,    31},  /* 5 = one intra neighbour + x */
   {25,      29,    30},  /* 6 = two intra neighbours */
};

static const unsigned char default_skip_probs[3] = {
   192, 128, 64
};

static const unsigned char default_intra_inter_p[4] = {
   9, 102, 187, 225
};

static const unsigned char default_comp_inter_p[5] = {
   239, 183, 119,  96,  41
};

static const unsigned char default_comp_ref_p[5] = {
   50, 126, 123, 221, 226
};

static const unsigned char default_tx_probs_32x32[2][3] = {
   { 3, 136, 37 },
   { 5, 52,  13 }
};

static const unsigned char default_tx_probs_16x16[2][2] = {
   { 20, 152 },
   { 15, 101 }
};

static const unsigned char default_tx_probs_8x8[2][1] = {
   { 100 },
   { 66  }
};

static const unsigned char default_nmv_joints[3] = {
   32, 64, 96
};

static const unsigned char default_nmv_components[2][33] = {
   {  /* Vertical component */
      128,                                                  /* sign */
      224, 144, 192, 168, 192, 176, 192, 198, 198, 245,     /* class */
      216,                                                  /* class0 */
      136, 140, 148, 160, 176, 192, 224, 234, 234, 240,     /* bits */
      128, 128, 64, 96, 112, 64,                            /* class0_fp */
      64, 96, 64,                                           /* fp */
      160,                                                  /* class0_hp bit */
      128,                                                  /* hp */
   },
   {  /* Horizontal component */
      128,                                                  /* sign */
      216, 128, 176, 160, 176, 176, 192, 198, 198, 208,     /* class */
      208,                                                  /* class0 */
      136, 140, 148, 160, 176, 192, 224, 234, 234, 240,     /* bits */
      128, 128, 64, 96, 112, 64,                            /* class0_fp */
      64, 96, 64,                                           /* fp */
      160,                                                  /* class0_hp bit */
      128,                                                  /* hp */
   }
};

/* ===== radeon_vcn_dec.c에서 ===== */

// Mesa의 fill_probs_table(void *ptr)과 같은 내용. 버퍼 맨 앞의 rvcn_dec_vp9_probs_t(2072바이트)만 채운다.
// 맨 앞 32바이트(nmvc_mask)는 0으로 지우고, 오프셋 32부터 2070까지(2039바이트) 기본 확률을 쓴다.
// 오프셋 2071은 구조체 끝의 정렬용 빈 바이트라 쓰지 않는다.
static inline void vp9_fill_probs_table(void *ptr)
{
   rvcn_dec_vp9_probs_t *probs = (rvcn_dec_vp9_probs_t *)ptr;

   memcpy(&probs->coef_probs[0], default_coef_probs_4x4, sizeof(default_coef_probs_4x4));
   memcpy(&probs->coef_probs[1], default_coef_probs_8x8, sizeof(default_coef_probs_8x8));
   memcpy(&probs->coef_probs[2], default_coef_probs_16x16, sizeof(default_coef_probs_16x16));
   memcpy(&probs->coef_probs[3], default_coef_probs_32x32, sizeof(default_coef_probs_32x32));
   memcpy(probs->y_mode_prob, default_if_y_probs, sizeof(default_if_y_probs));
   memcpy(probs->uv_mode_prob, default_if_uv_probs, sizeof(default_if_uv_probs));
   memcpy(probs->single_ref_prob, default_single_ref_p, sizeof(default_single_ref_p));
   memcpy(probs->switchable_interp_prob, default_switchable_interp_prob,
          sizeof(default_switchable_interp_prob));
   memcpy(probs->partition_prob, default_partition_probs, sizeof(default_partition_probs));
   memcpy(probs->inter_mode_probs, default_inter_mode_probs, sizeof(default_inter_mode_probs));
   memcpy(probs->mbskip_probs, default_skip_probs, sizeof(default_skip_probs));
   memcpy(probs->intra_inter_prob, default_intra_inter_p, sizeof(default_intra_inter_p));
   memcpy(probs->comp_inter_prob, default_comp_inter_p, sizeof(default_comp_inter_p));
   memcpy(probs->comp_ref_prob, default_comp_ref_p, sizeof(default_comp_ref_p));
   memcpy(probs->tx_probs_32x32, default_tx_probs_32x32, sizeof(default_tx_probs_32x32));
   memcpy(probs->tx_probs_16x16, default_tx_probs_16x16, sizeof(default_tx_probs_16x16));
   memcpy(probs->tx_probs_8x8, default_tx_probs_8x8, sizeof(default_tx_probs_8x8));
   memcpy(probs->mv_joints, default_nmv_joints, sizeof(default_nmv_joints));
   memcpy(&probs->mv_comps[0], default_nmv_components, sizeof(default_nmv_components));
   memset(&probs->nmvc_mask, 0, sizeof(rvcn_dec_vp9_nmv_ctx_mask_t));
}

/* ===== 여기부터는 Mesa에 없는 묶음 함수 ===== */

// Mesa가 VP9용으로 잡는 ctx 버퍼 크기(rvcn_dec_message_decode()의 VP9 분기, VCN 2.0 이상).
//   ctx_size  = 2304 * 5                  기본 확률 + 확률 데이터(문맥 4개 + 현재)      11,520
//   ctx_size += 32 * 2 * 128 * 68         SRE collocated context data                     557,056
//   ctx_size += 9 * 64 * 2 * 128 * 68     SMP collocated context data                  10,027,008
//   ctx_size += 8 * 2 * 2 * 8192          SDB left tile pixel                             262,144
//   profile 2(10비트)면 ctx_size += 8 * 2 * 4096                                           65,536
// 합계: profile 0은 10,857,728바이트, profile 2는 10,923,264바이트. 해상도와 무관한 고정 크기다.
// (VCN 1.x는 128 * 68 자리에 64 * 64, 8 * 2 * 2 * 8192 자리에 8 * 2 * 4096을 쓴다. Rembrandt는 해당 없음.)
// 이 값이 rvcn_dec_message_decode_t.hw_ctxt_size로 들어간다(Mesa는 실제 할당된 버퍼 크기를 넣는다).
#define VP9_HW_CONTEXT_SIZE(is_profile2) \
   (2304 * 5 + 32 * 2 * 128 * 68 + 9 * 64 * 2 * 128 * 68 + 8 * 2 * 2 * 8192 + ((is_profile2) ? 8 * 2 * 4096 : 0))
_Static_assert(VP9_HW_CONTEXT_SIZE(0) == 10857728, "ctx size profile 0");
_Static_assert(VP9_HW_CONTEXT_SIZE(1) == 10923264, "ctx size profile 2");

// ctx 버퍼 초기화. Mesa는 ctx 버퍼를 처음 만들 때(스트림당 한 번) fill_probs_table(ptr)만 부른다.
// 채우는 범위: ctx_buf의 [0, 2071). 그중 [0, 32)는 0, [32, 2071)은 기본 확률. sizeof(rvcn_dec_vp9_probs_t)는 2072.
// 나머지 [2071, VP9_HW_CONTEXT_SIZE)는 Mesa가 건드리지 않는다(새로 할당한 GPU 버퍼라 0으로 차 있다고 보면 된다).
// 호출자는 버퍼를 0으로 지운 뒤 이 함수를 부르는 것이 안전하다. 해상도가 바뀌어도 Mesa는 ctx를 다시 만들지 않는다.
static inline void vp9_init_hw_context(void *ctx_buf)
{
   vp9_fill_probs_table(ctx_buf);
}

// 확률 버퍼(메시지 버퍼의 FB_BUFFER_OFFSET + FB_BUFFER_SIZE 위치, 2560바이트) 초기화.
// Mesa는 디코더를 만들 때 메시지 버퍼 NUM_BUFFERS개 각각의 확률 구역에도 같은 fill_probs_table()을 한 번씩 부른다.
// 그 뒤 프레임마다 뒤쪽 256바이트(세그먼트 구역)만 get_vp9_msg()가 고쳐 쓴다(vp9msg.h의 vp9_fill_msg).
static inline void vp9_init_probs_buffer(void *probs_buf)
{
   memset(probs_buf, 0, VP9_PROBS_TABLE_SIZE);
   vp9_fill_probs_table(probs_buf);
}

#endif /* VP9_PROBS_H */
