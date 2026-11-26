#!/usr/bin/env python3
"""gen_adb_key.py — generate an ADB RSA-2048 keypair for provisioning.

Output: raw modulus + private exponent as binary files sized for NVS burn,
plus the "QCN1 user@host" public blob expected by adbd.

Usage: python3 tools/gen_adb_key.py [--out out_dir] [--user p4phonehub@waveshare]
"""
import argparse, base64, os, sys

try:
    from cryptography.hazmat.primitives.asymmetric import rsa
except ImportError:
    sys.exit("pip install cryptography")

p = argparse.ArgumentParser()
p.add_argument("--out", default="keys")
p.add_argument("--user", default="p4phonehub@waveshare")
a = p.parse_args()

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
m = key.public_key().public_numbers().n
mod = m.to_bytes(256, "big")
d = key.private_numbers().d.to_bytes(256, "big")

os.makedirs(a.out, exist_ok=True)
open(f"{a.out}/adb_modulus.bin", "wb").write(mod)
open(f"{a.out}/adb_privexp.bin", "wb").write(d)

# adbd expects the same wire form as its own keystore files:
# "user@host\0" + base64 androidpubkey; QCN1 prefix per adb spec revisions.
pub_blob = base64.b64encode(mod).decode() + " " + a.user + "\x01"
open(f"{a.out}/adb_pubblob.txt", "w").write("QCN1 " + pub_blob)
print("keypair written:", a.out)
