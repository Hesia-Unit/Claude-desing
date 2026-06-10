#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import struct
import time
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

HESIA_RECOVERY_MAGIC = 0x48535231
HESIA_RECOVERY_VERSION = 2
HESIA_RECOVERY_CHALLENGE_FMT = "<IIQ32s65s7x"
HESIA_RECOVERY_TOKEN_FMT = "<IIQ32s32s32s64s"


def load_bytes(path: Path) -> bytes:
    return path.read_bytes()


def load_private_key(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey):
        raise TypeError("recovery key must be an EC private key")
    if not isinstance(key.curve, ec.SECP256R1):
        raise TypeError("recovery key must use P-256 / secp256r1")
    return key


def decode_challenge(blob: bytes) -> tuple[bytes, bytes, int]:
    expected = struct.calcsize(HESIA_RECOVERY_CHALLENGE_FMT)
    if len(blob) != expected:
        raise ValueError(f"challenge size mismatch: got {len(blob)}, expected {expected}")
    magic, version, expires_at, nonce, attest_pub = struct.unpack(HESIA_RECOVERY_CHALLENGE_FMT, blob)
    if magic != HESIA_RECOVERY_MAGIC:
        raise ValueError(f"unexpected challenge magic: 0x{magic:08x}")
    if version != HESIA_RECOVERY_VERSION:
        raise ValueError(f"unexpected challenge version: {version}")
    if expires_at <= 0:
        raise ValueError("challenge expiry is invalid")
    if len(attest_pub) != 65:
        raise ValueError("challenge attestation public key is malformed")
    if any(attest_pub) and attest_pub[0] != 0x04:
        raise ValueError("challenge attestation public key is malformed")
    return nonce, attest_pub, expires_at


def build_token(
    nonce: bytes,
    attest_pub: bytes,
    expires_at: int,
    secret: bytes,
    key: ec.EllipticCurvePrivateKey,
) -> bytes:
    if len(secret) != 32:
        raise ValueError("new secret must be exactly 32 bytes")
    if int(time.time()) > expires_at:
        raise ValueError("challenge already expired")

    new_secret_hash = hashlib.sha256(secret).digest()
    attest_hash = hashlib.sha256(attest_pub).digest()
    unsigned = struct.pack(
        "<IIQ32s32s32s",
        HESIA_RECOVERY_MAGIC,
        HESIA_RECOVERY_VERSION,
        expires_at,
        nonce,
        new_secret_hash,
        attest_hash,
    )
    digest = hashlib.sha256(unsigned).digest()
    der_sig = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der_sig)
    raw_sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    if len(raw_sig) != 64:
        raise ValueError("unexpected raw signature size")
    return struct.pack(
        HESIA_RECOVERY_TOKEN_FMT,
        HESIA_RECOVERY_MAGIC,
        HESIA_RECOVERY_VERSION,
        expires_at,
        nonce,
        new_secret_hash,
        attest_hash,
        raw_sig,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate an offline OP-TEE recovery token for HESIA session-auth recovery."
    )
    parser.add_argument("--challenge", required=True, type=Path, help="binary challenge file from hesia_ca recovery_challenge")
    parser.add_argument("--secret", required=True, type=Path, help="32-byte new OP-TEE session auth secret")
    parser.add_argument("--key", required=True, type=Path, help="offline recovery private key (PEM, P-256)")
    parser.add_argument("--out", required=True, type=Path, help="output token path")
    args = parser.parse_args()

    nonce, attest_pub, expires_at = decode_challenge(load_bytes(args.challenge))
    secret = load_bytes(args.secret)
    key = load_private_key(args.key)
    token = build_token(nonce, attest_pub, expires_at, secret, key)
    args.out.write_bytes(token)

    print(f"challenge: {args.challenge}")
    print(f"secret:    {args.secret}")
    print(f"token:     {args.out}")
    print(f"nonce:     {nonce.hex()}")
    print(f"expires:   {expires_at}")
    print(f"attest:    {hashlib.sha256(attest_pub).hexdigest()}")
    print(f"secret:    {hashlib.sha256(secret).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
