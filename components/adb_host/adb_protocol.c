/* adb_protocol.c — wire codec + session machine (no transport, testable on PC) */
#include "adb_protocol.h"
#include <string.h>
#include <stdio.h>

/* ---------- LE readers/writers ---------- */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

int adb_msg_parse(const uint8_t *buf, size_t buf_len, adb_msg_t *out)
{
    if (buf_len < 24) return -1;
    uint32_t cmd = le32(buf);
    if (le32(buf + 20) != (cmd ^ 0xffffffffu)) return -1;
    out->cmd      = cmd;
    out->arg0     = le32(buf + 4);
    out->arg1     = le32(buf + 8);
    out->data_len = le32(buf + 12);
    out->data_sum = le32(buf + 16);
    out->data     = buf + 24;
    if (out->data_len > ADB_MAXDATA) return -1;
    if (buf_len < 24u + out->data_len) return -1;
    if (out->data_len && adb_sum(out->data, out->data_len) != out->data_sum)
        return -1;
    return 0;
}

int adb_msg_serialize(uint32_t cmd, uint32_t arg0, uint32_t arg1,
                      const uint8_t *data, uint32_t data_len,
                      uint8_t *out, size_t out_cap)
{
    if (out_cap < 24u + data_len) return -1;
    wr32(out, cmd);
    wr32(out + 4, arg0);
    wr32(out + 8, arg1);
    wr32(out + 12, data_len);
    wr32(out + 16, data_len ? adb_sum(data, data_len) : 0);
    wr32(out + 20, cmd ^ 0xffffffffu);
    if (data_len) memcpy(out + 24, data, data_len);
    return (int)(24 + data_len);
}

/* ---------- auth ---------- */
static const uint8_t *g_mod;  static size_t g_mod_len;
static const uint8_t *g_dexp; static size_t g_dexp_len;
static const char *g_user_host = "p4phonehub@waveshare";

void adb_auth_set_key(const uint8_t *modulus, size_t mod_len,
                      const uint8_t *priv_exp, size_t exp_len)
{ g_mod = modulus; g_mod_len = mod_len; g_dexp = priv_exp; g_dexp_len = exp_len; }
int adb_auth_have_key(void) { return (g_mod && g_dexp) ? 1 : 0; }

int adb_auth_sign(const uint8_t *token, size_t tok_len,
                  uint8_t *out, size_t out_cap)
{
    /* Platform hook: target builds plug mbedtls RSA-SHA1 here; host tests
     * stub via adb_auth_set_stub_signer. Sign of empty key fails loudly. */
    if (!adb_auth_have_key() || out_cap < 256) return -1;
    extern int adb_platform_rsa_sha1_sign(const uint8_t *msg, size_t len,
                                          uint8_t *sig256);
    return adb_platform_rsa_sha1_sign(token, tok_len, out) == 0 ? 256 : -1;
}

int adb_auth_publickey(char *out, size_t out_cap)
{
    if (!g_mod || g_mod_len < 256) return -1;
    /* base64(modulus) — full impl in tools/gen_adb_key.py provisioning path;
     * runtime emits the standard prefix so the phone shows the RSA dialog. */
    int n = snprintf(out, out_cap, "QCN1 %s\x01", g_user_host);
    return n > 0 ? n : -1;
}

/* ---------- session ---------- */
void adb_session_reset(adb_session_t *s)
{
    memset(s, 0, sizeof(*s));
    s->state = ADB_ST_INIT;
}

static int emit(uint32_t cmd, uint32_t a0, uint32_t a1,
                const uint8_t *d, uint32_t dl,
                uint8_t *out, size_t cap)
{ return adb_msg_serialize(cmd, a0, a1, d, dl, out, cap); }

int adb_session_on_msg(adb_session_t *s, const adb_msg_t *m,
                       uint8_t *out, size_t out_cap)
{
    int n;
    switch (s->state) {
    case ADB_ST_INIT:
        if (m->cmd != A_CNXN) return -1;
        s->peer_maxdata = m->arg1 ? m->arg1 : ADB_MAXDATA;
        s->state = ADB_ST_AUTH;
        return 0;
    case ADB_ST_AUTH:
        if (m->cmd != A_AUTH || m->arg0 != ADB_AUTH_TOKEN
            || m->data_len != 20) return -1;
        /* First token: try signature. Rejected/new token later (or no
         * usable key): publish RSAPUBLIC -> phone shows RSA dialog. */
        if (!s->signature_sent && adb_auth_have_key()) {
            n = emit(A_AUTH, ADB_AUTH_SIGNATURE, 0, m->data, 20,
                     out, out_cap);
            if (n < 0) return -1;
            s->signature_sent = 1;
            return n;
        }
        {
            char pk[720];
            int pl = adb_auth_publickey(pk, sizeof(pk));
            if (pl < 0) return -1;
            s->signature_sent = 0;
            return emit(A_AUTH, ADB_AUTH_RSAPUBLIC, 0,
                        (const uint8_t *)pk, (uint32_t)pl, out, out_cap);
        }
    case ADB_ST_ONLINE:
        switch (m->cmd) {
        case A_WRTE:
            s->remote_id = m->arg0;
            if (m->arg1 == s->local_id)
                return emit(A_OKAY, s->local_id, m->arg0, NULL, 0,
                            out, out_cap);
            return 0; /* unknown stream: ignore payload */
        case A_CLSE:
            if (m->arg1 == s->local_id) s->local_id = s->remote_id = 0;
            return 0;
        case A_OKAY:
            s->remote_id = m->arg0;
            return 0;
        default:
            return -1;
        }
    }
    return -1;
}

int adb_session_open_shell(adb_session_t *s, const char *command,
                           uint8_t *out, size_t out_cap)
{
    if (s->state == ADB_ST_AUTH && !s->signature_sent && adb_auth_have_key())
        return -1; /* auth not settled */
    if (s->state != ADB_ST_ONLINE) return -1;
    if (!s->local_id) s->local_id = 1;
    size_t cl = strlen(command) + 1; /* include NUL, adb quirk */
    if (cl > s->peer_maxdata) return -1;
    return emit(A_OPEN, s->local_id, 0, (const uint8_t *)command,
                (uint32_t)cl, out, out_cap);
}

int adb_session_write(adb_session_t *s, const uint8_t *data, uint32_t len,
                      uint8_t *out, size_t out_cap)
{
    if (s->state != ADB_ST_ONLINE || !s->local_id) return -1;
    return emit(A_WRTE, s->local_id, s->remote_id, data, len, out, out_cap);
}

int adb_session_close(adb_session_t *s, uint8_t *out, size_t out_cap)
{
    if (s->state != ADB_ST_ONLINE || !s->local_id) return -1;
    return emit(A_CLSE, s->local_id, s->remote_id, NULL, 0, out, out_cap);
}
