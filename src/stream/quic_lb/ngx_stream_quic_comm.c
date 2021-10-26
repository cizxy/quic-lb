
/*
 * Copyright 2020. All Rights Reserved.
 * Author: william.zk
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

static ngx_inline u_char *ngx_quic_parse_int(u_char *pos, u_char *end, uint64_t *out);


u_char *
ngx_quic_read_uint8(u_char *pos, u_char *end, uint8_t *value)
{
    if ((size_t)(end - pos) < 1) {
        return NULL;
    }

    *value = *pos;

    return pos + 1;
}


u_char *
ngx_quic_read_uint32(u_char *pos, u_char *end, uint32_t *value)
{
    if ((size_t)(end - pos) < sizeof(uint32_t)) {
        return NULL;
    }

    *value = ngx_quic_parse_uint32(pos);

    return pos + sizeof(uint32_t);
}


static ngx_inline u_char *
ngx_quic_parse_int(u_char *pos, u_char *end, uint64_t *out)
{
    u_char      *p;
    uint64_t     value;
    ngx_uint_t   len;

    if (pos >= end) {
        return NULL;
    }

    p = pos;
    len = 1 << ((*p & 0xc0) >> 6);

    value = *p++ & 0x3f;

    if ((size_t)(end - p) < (len - 1)) {
        return NULL;
    }

    while (--len) {
        value = (value << 8) + *p++;
    }

    *out = value;

    return p;
}


u_char *
ngx_quic_read_bytes(u_char *pos, u_char *end, size_t len, u_char **out)
{
    if ((size_t)(end - pos) < len) {
        return NULL;
    }

    *out = pos;

    return pos + len;
}


ngx_int_t
ngx_quic_parse_initial_header(ngx_quic_header_t *pkt)
{
    u_char    *p, *end;
    uint64_t   varint;

    p = pkt->raw->pos;

    end = pkt->raw->last;

    pkt->log->action = "QUIC-LB, parsing quic initial header";

    p = ngx_quic_parse_int(p, end, &varint);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_INFO, pkt->log, 0,
                      "QUIC-LB, quic failed to parse token length");
        return NGX_ERROR;
    }

    pkt->token.len = varint;

    p = ngx_quic_read_bytes(p, end, pkt->token.len, &pkt->token.data);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_INFO, pkt->log, 0,
                      "QUIC-LB, quic packet too small to read token data");
        return NGX_ERROR;
    }

    p = ngx_quic_parse_int(p, end, &varint);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_INFO, pkt->log, 0, "QUIC-LB, quic bad packet length");
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, pkt->log, 0,
                   "QUIC-LB, quic initial packet length: %uL", varint);

    if (varint > (uint64_t) ((pkt->data + pkt->len) - p)) {
        ngx_log_error(NGX_LOG_INFO, pkt->log, 0,
                      "QUIC-LB, quic truncated initial packet");
        return NGX_ERROR;
    }

    pkt->raw->pos = p;
    pkt->len = varint;

    return NGX_OK;
}


ngx_int_t
ngx_quic_hexstring_to_string(u_char *dst, u_char *src, ngx_int_t src_len)
{
    ngx_int_t  i, len;
    ngx_int_t  rc;

    if (dst == NULL || src == NULL || src_len < 0) {
        return NGX_ERROR;
    }

    if (src_len % 2 != 0) {
        return NGX_ERROR;
    }

    len = src_len / 2;

    for (i = 0; i < len; i++) {
        rc = ngx_hextoi(src + (2 * i), 2);
        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }
        dst[i] = (u_char)rc;
    }

    return NGX_OK;
}

ngx_int_t
ngx_quic_aes_128_ecb_encrypt(u_char *plaintext, ngx_int_t plaintext_len,
    u_char *key, u_char *ciphertext)
{
    EVP_CIPHER_CTX   *ctx;
    int               len;
    ngx_int_t         ciphertext_len;

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        goto failed;
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL) <= 0 ) {
        goto failed;
    }

    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len) <= 0) {
        goto failed;
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) <= 0) {
        goto failed;
    }
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
failed:
    EVP_CIPHER_CTX_free(ctx);
    return NGX_ERROR;
}
