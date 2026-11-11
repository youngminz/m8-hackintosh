#!/bin/bash
# M8 Ubuntu live: open SSH for the work computer (put your own public key in PUBKEY)
set -e
PUBKEY='ssh-ed25519 CHANGE_ME_YOUR_PUBLIC_KEY you@example'
if ! dpkg -s openssh-server >/dev/null 2>&1; then
  for i in $(seq 1 30); do ping -c1 -W2 archive.ubuntu.com >/dev/null 2>&1 && break; sleep 2; done
  apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq openssh-server
fi
H=/home/ubuntu/.ssh; mkdir -p $H
grep -q "$PUBKEY" $H/authorized_keys 2>/dev/null || \
  echo "$PUBKEY" >> $H/authorized_keys
chown -R 1000:1000 $H; chmod 700 $H; chmod 600 $H/authorized_keys
systemctl enable --now ssh >/dev/null 2>&1 || systemctl start ssh
echo "SSH ready: $(hostname -I)"
