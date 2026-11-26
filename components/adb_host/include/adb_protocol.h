/**
 * adb_protocol.h — ADB wire protocol core (transport-agnostic).
 *
 * Message layout (little-endian, all fields u32):
 *   cmd(4) arg0(4) arg1(4) data_len(4) data_sum(4) magic(4) data[data_len]
 *   magic = cmd ^ 0xFFFFFFFF
 *   data_sum = sum of data bytes (NOT crc32 — adb spec quirk)
 *
 * Commands: CNXN AUTH OPEN OKAY CLSE WRTE
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define ADB_VERSION      0x01000001u
#define ADB_MAXDATA      4096u
#define ADB_MAX_PAYLOAD  (ADB_MAXDATA + 24u)

enum adb_cmd {
    A_CNXN = 0x4e584e43u, /* 'CNXN' */
    A_AUTH = 0x54485541u, /* 'AUTH' */
    A_OPEN = 0x4e45504fu, /* 'OPEN' */
    A_OKAY = 0x59414b4fu, /* 'OKAY' */
    A_CLSE = 0x45534c43u, /* 'CLSE' */
    A_WRTE = 0x45545257u, /* 'WRTE' */
};

enum adb_auth_type {
    ADB_AUTH_TOKEN     = 1, /* peer sends 20B token, we reply SIGNATURE */
    ADB_AUTH_SIGNATURE = 2,
    ADB_AUTH_RSAPUBLIC = 3,
};

/* Parsed wire message (no payload copy: points into caller buffer). */
typedef struct {
    uint32_t cmd, arg0, arg1, data_len, data_sum;
    const uint8_t *data; /* valid only inside the source buffer lifetime */
} adb_msg_t;

/* ---- checksum ---- */
static inline uint32_t adb_sum(const uint8_t *d, size_t n)
{
    uint32_t s = 0;
    for (size_t i = 0; i < n; i++) s += d[i];
    return s;
}

/* Parse one 24B header + validate magic & sum. Returns 0 ok, -1 bad. */
int adb_msg_parse(const uint8_t *buf, size_t buf_len, adb_msg_t *out);

/* Serialize header (24B) into out; returns bytes written (24). */
int adb_msg_serialize(uint32_t cmd, uint32_t arg0, uint32_t arg1,
                      const uint8_t *data, uint32_t data_len,
                      uint8_t *out, size_t out_cap);

/* ---- auth ---- */
/* Load RSA key from raw components (modulus+priv_exp, 2048-bit).
 * Kept as byte arrays so NVS/provisioned storage can feed either PEM or raw. */
void adb_auth_set_key(const uint8_t *modulus, size_t mod_len,
                      const uint8_t *priv_exp, size_t exp_len);
/* Has a usable private key been provisioned? */
int  adb_auth_have_key(void);
/* Sign 20B token -> 256B RSA-SHA1 signature (out_cap>=256). Returns 256 or -1. */
int  adb_auth_sign(const uint8_t *token, size_t tok_len,
                   uint8_t *out, size_t out_cap);
/* Build "QCN1 user@host\0" public key blob (adb recognizes this form).
 * out_cap>=720. Returns blob length or -1. */
int  adb_auth_publickey(char *out, size_t out_cap);

/* ---- session state machine ---- */
typedef enum {
    ADB_ST_INIT,      /* expect CNXN */
    ADB_ST_AUTH,      /* expect AUTH TOKEN -> send SIGNATURE/RSAPUBLIC */
    ADB_ST_ONLINE,    /* OPEN/WRTE/OKAY/CLSE exchange */
} adb_state_t;

typedef struct {
    adb_state_t state;
    uint32_t local_id, remote_id;
    uint32_t peer_maxdata;
    int  signature_sent;
} adb_session_t;

void    adb_session_reset(adb_session_t *s);
/* Feed one inbound message; fills outbound frames into out (appended).
 * Returns number of outbound bytes, or -1 on protocol error. */
int     adb_session_on_msg(adb_session_t *s, const adb_msg_t *m,
                           uint8_t *out, size_t out_cap);
/* Helpers to enqueue requests while ONLINE. */
int adb_session_open_shell(adb_session_t *s, const char *command,
                           uint8_t *out, size_t out_cap);
int adb_session_write(adb_session_t *s, const uint8_t *data, uint32_t len,
                      uint8_t *out, size_t out_cap);
int adb_session_close(adb_session_t *s, uint8_t *out, size_t out_cap);
