/* test_protocol.c — PC unit tests for adb protocol core (gcc, no HW) */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../components/adb_host/include/adb_protocol.h"

/* stub signer for host tests */
int adb_platform_rsa_sha1_sign(const uint8_t *msg, size_t len, uint8_t *sig256)
{
    (void)msg; (void)len;
    memset(sig256, 0xA5, 256);
    return 0;
}

static uint8_t txbuf[8192];

static void wr(uint8_t *p, uint32_t v)
{ p[0]=v&0xff; p[1]=(v>>8)&0xff; p[2]=(v>>16)&0xff; p[3]=(v>>24)&0xff; }

static void build_msg(uint8_t *buf, uint32_t cmd, uint32_t a0, uint32_t a1,
                      const uint8_t *data, uint32_t dl)
{
    wr(buf, cmd); wr(buf+4,a0); wr(buf+8,a1); wr(buf+12,dl);
    wr(buf+16, dl ? adb_sum(data,dl) : 0); wr(buf+20, cmd^0xffffffffu);
    if (dl) memcpy(buf+24, data, dl);
}

static void test_roundtrip(void)
{
    const uint8_t data[] = {1,2,3,4,5};
    uint8_t buf[64];
    int n = adb_msg_serialize(A_CNXN, 0x01000001, 4096, data, 5, buf, sizeof buf);
    assert(n == 29);
    adb_msg_t m;
    assert(adb_msg_parse(buf, (size_t)n, &m) == 0);
    assert(m.cmd == A_CNXN && m.arg1 == 4096 && m.data_len == 5);
    assert(m.data_sum == adb_sum(data,5));
    printf("PASS roundtrip\n");
}

static void test_reject_corrupt(void)
{
    uint8_t buf[64]; build_msg(buf, A_CNXN, 1, 2, NULL, 0);
    buf[20] ^= 0xff;                       /* break magic */
    adb_msg_t m;
    assert(adb_msg_parse(buf, 24, &m) == -1);
    build_msg(buf, A_CNXN, 1, 2, (const uint8_t*)"hi", 2);
    buf[16] ^= 0x01;                       /* break sum */
    assert(adb_msg_parse(buf, 26, &m) == -1);
    printf("PASS reject corrupt\n");
}

static void test_handshake_signature(void)
{
    uint8_t mod[256], dexp[256];
    memset(mod, 0x11, sizeof mod); memset(dexp, 0x22, sizeof dexp);
    adb_auth_set_key(mod, sizeof mod, dexp, sizeof dexp);
    assert(adb_auth_have_key() == 1);

    adb_session_t s; adb_session_reset(&s);
    uint8_t cnxn[64]; build_msg(cnxn, A_CNXN, 0x01000001, 4096, NULL, 0);
    adb_msg_t m; assert(adb_msg_parse(cnxn, 24, &m) == 0);
    int n = adb_session_on_msg(&s, &m, txbuf, sizeof txbuf);
    assert(n == 0 && s.state == ADB_ST_AUTH && s.peer_maxdata == 4096);

    uint8_t token[20+24]; build_msg(token, A_AUTH, 1, 0,
        (const uint8_t*)"0123456789abcdefghij", 20);
    assert(adb_msg_parse(token, 44, &m) == 0);
    n = adb_session_on_msg(&s, &m, txbuf, sizeof txbuf);
    assert(n == 24+20);
    adb_msg_t om; assert(adb_msg_parse(txbuf, (size_t)n, &om) == 0);
    assert(om.cmd == A_AUTH && om.arg0 == ADB_AUTH_SIGNATURE);
    assert(om.data_len == 20); /* token echoed into signer */
    printf("PASS handshake signature\n");
}

static void test_handshake_pubkey_fallback(void)
{
    /* Signature rejected by phone (unauthorized device) -> RSAPUBLIC */
    uint8_t mod[256], dexp[256];
    memset(mod, 0x11, sizeof mod); memset(dexp, 0x22, sizeof dexp);
    adb_auth_set_key(mod, sizeof mod, dexp, sizeof dexp);

    adb_session_t s; adb_session_reset(&s);
    uint8_t cnxn[64]; build_msg(cnxn, A_CNXN, 1, 4096, NULL, 0);
    adb_msg_t m; assert(adb_msg_parse(cnxn, 24, &m) == 0);
    assert(adb_session_on_msg(&s, &m, txbuf, sizeof txbuf) == 0);

    uint8_t token[44]; build_msg(token, A_AUTH, 1, 0,
        (const uint8_t*)"0123456789abcdefghij", 20);
    assert(adb_msg_parse(token, 44, &m) == 0);
    int n = adb_session_on_msg(&s, &m, txbuf, sizeof txbuf);
    assert(n == 44);
    assert(adb_msg_parse(txbuf, (size_t)n, &m) == 0);
    assert(m.cmd == A_AUTH && m.arg0 == ADB_AUTH_SIGNATURE);

    /* phone rejects (no user confirm yet): sends a NEW token */
    assert(adb_msg_parse(token, 44, &m) == 0);
    n = adb_session_on_msg(&s, &m, txbuf, sizeof txbuf);
    assert(n > 24);
    assert(adb_msg_parse(txbuf, (size_t)n, &m) == 0);
    assert(m.cmd == A_AUTH && m.arg0 == ADB_AUTH_RSAPUBLIC);
    printf("PASS pubkey fallback\n");
}

static void test_online_shell_flow(void)
{
    uint8_t mod[256], dexp[256];
    memset(mod, 0x11, 256); memset(dexp, 0x22, 256);
    adb_auth_set_key(mod, 256, dexp, 256);
    adb_session_t s; adb_session_reset(&s);
    s.state = ADB_ST_ONLINE; s.peer_maxdata = 4096; s.local_id = 1;

    int n = adb_session_open_shell(&s, "echo hi", txbuf, sizeof txbuf);
    assert(n == 24+8);
    adb_msg_t m; assert(adb_msg_parse(txbuf, (size_t)n, &m) == 0);
    assert(m.cmd == A_OPEN && m.arg0 == 1);
    printf("NOTE open shell emitted (local=%u)\n", m.arg0);

    /* phone accepts stream: OKAY(remote=2, local=1) */
    uint8_t okay[24]; build_msg(okay, A_OKAY, 2, 1, NULL, 0);
    assert(adb_msg_parse(okay, 24, &m) == 0);
    n = adb_session_on_msg(&s, &m, txbuf, sizeof txbuf);
    assert(n == 0 && s.remote_id == 2);

    /* phone sends output: WRTE(remote=2, local=1, "hi\n") */
    uint8_t wrte[24+3]; build_msg(wrte, A_WRTE, 2, 1,
        (const uint8_t*)"hi\n", 3);
    assert(adb_msg_parse(wrte, 27, &m) == 0);
    n = adb_session_on_msg(&s, &m, txbuf, sizeof txbuf);
    assert(n == 24);
    assert(adb_msg_parse(txbuf, 24, &m) == 0 && m.cmd == A_OKAY);

    /* close */
    n = adb_session_close(&s, txbuf, sizeof txbuf);
    assert(n == 24);
    assert(adb_msg_parse(txbuf, 24, &m) == 0 && m.cmd == A_CLSE);
    printf("PASS online shell flow\n");
}

int main(void)
{
    test_roundtrip();
    test_reject_corrupt();
    test_handshake_signature();
    test_handshake_pubkey_fallback();
    test_online_shell_flow();
    printf("ALL PROTOCOL TESTS PASSED\n");
    return 0;
}
