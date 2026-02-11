#include "vcs.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define VELOCE_PATH_SEP '\\'
#else
#define VELOCE_PATH_SEP '/'
#endif

static char g_storage_root[VELOCE_PATH_LEN + 1];
static int g_storage_ready = 0;

static void seed_rng_once(void)
{
    static int seeded = 0;
    unsigned int seed;

    if (seeded)
    {
        return;
    }

    seed = (unsigned int)time(NULL);
    seed ^= (unsigned int)clock();
    seed ^= (unsigned int)(uintptr_t)&seeded;
    srand(seed);
    seeded = 1;
}

const char *storage_root(void)
{
    const char *env_home;

    if (g_storage_root[0] != '\0')
    {
        return g_storage_root;
    }

    env_home = getenv("VELOCE_HOME");
    if (env_home != NULL && env_home[0] != '\0')
    {
        (void)snprintf(g_storage_root, sizeof(g_storage_root), "%s", env_home);
    }
    else
    {
        (void)snprintf(g_storage_root, sizeof(g_storage_root), ".veloce");
    }

    return g_storage_root;
}

int path_join(char *out, size_t out_size, const char *left, const char *right)
{
    size_t left_len;
    int needed;

    if (out == NULL || out_size == 0 || left == NULL || right == NULL)
    {
        return -1;
    }

    left_len = strlen(left);
    if (left_len > 0 && left[left_len - 1] == VELOCE_PATH_SEP)
    {
        needed = snprintf(out, out_size, "%s%s", left, right);
    }
    else
    {
        needed = snprintf(out, out_size, "%s%c%s", left, VELOCE_PATH_SEP, right);
    }

    if (needed < 0 || (size_t)needed >= out_size)
    {
        return -1;
    }

    return 0;
}

int ensure_dir(const char *path)
{
    int res;

    if (path == NULL || path[0] == '\0')
    {
        return -1;
    }

#ifdef _WIN32
    res = _mkdir(path);
#else
    res = mkdir(path, 0775);
#endif

    if (res == 0 || errno == EEXIST)
    {
        return 0;
    }

    return -1;
}

int file_exists(const char *path)
{
    FILE *fp;

    if (path == NULL)
    {
        return 0;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return 0;
    }

    fclose(fp);
    return 1;
}

static int touch_file(const char *path)
{
    FILE *fp;

    fp = fopen(path, "ab");
    if (fp == NULL)
    {
        return -1;
    }

    fclose(fp);
    return 0;
}

int ensure_storage_ready(void)
{
    char path[VELOCE_PATH_LEN + 1];

    if (g_storage_ready)
    {
        return 0;
    }

    if (ensure_dir(storage_root()) != 0)
    {
        return -1;
    }

    if (path_join(path, sizeof(path), storage_root(), VELOCE_SNAPSHOTS_DIR) != 0 ||
        ensure_dir(path) != 0)
    {
        return -1;
    }

    if (path_join(path, sizeof(path), storage_root(), VELOCE_WORKSPACE_DIR) != 0 ||
        ensure_dir(path) != 0)
    {
        return -1;
    }

    if (path_join(path, sizeof(path), storage_root(), VELOCE_USERS_DB) != 0 ||
        touch_file(path) != 0)
    {
        return -1;
    }

    if (path_join(path, sizeof(path), storage_root(), VELOCE_REPOS_DB) != 0 ||
        touch_file(path) != 0)
    {
        return -1;
    }

    if (path_join(path, sizeof(path), storage_root(), VELOCE_COMMITS_DB) != 0 ||
        touch_file(path) != 0)
    {
        return -1;
    }

    g_storage_ready = 1;
    return 0;
}

void app_clear_screen(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD home = {0, 0};
    DWORD written;
    DWORD cells;
    HANDLE hstdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (hstdout == INVALID_HANDLE_VALUE)
    {
        return;
    }

    if (!GetConsoleScreenBufferInfo(hstdout, &csbi))
    {
        return;
    }

    cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
    FillConsoleOutputCharacterA(hstdout, ' ', cells, home, &written);
    FillConsoleOutputAttribute(hstdout, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(hstdout, home);
#else
    (void)printf("\033[2J\033[H");
    fflush(stdout);
#endif
}

void app_sleep_ms(unsigned int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&req, NULL);
#endif
}

int app_getch(void)
{
#ifdef _WIN32
    return _getch();
#else
    int ch;
    struct termios oldt;
    struct termios newt;

    if (tcgetattr(STDIN_FILENO, &oldt) != 0)
    {
        return getchar();
    }

    newt = oldt;
    newt.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

void app_pause(const char *prompt)
{
    const char *msg = prompt != NULL ? prompt : "Press any key to continue...";
    (void)printf("%s", msg);
    (void)fflush(stdout);
    (void)app_getch();
    (void)printf("\n");
}

static void consume_stdin_until_newline(void)
{
    int ch;

    do
    {
        ch = getchar();
    } while (ch != '\n' && ch != EOF);
}

int read_line(const char *prompt, char *buffer, size_t size)
{
    size_t len;

    if (buffer == NULL || size < 2U)
    {
        return 0;
    }

    if (prompt != NULL)
    {
        (void)printf("%s", prompt);
    }

    if (fgets(buffer, (int)size, stdin) == NULL)
    {
        return 0;
    }

    len = strlen(buffer);
    if (len > 0U && buffer[len - 1U] == '\n')
    {
        buffer[len - 1U] = '\0';
    }
    else
    {
        consume_stdin_until_newline();
    }

    return 1;
}

int read_password(const char *prompt, char *buffer, size_t size)
{
    size_t i = 0U;
    int ch;

    if (buffer == NULL || size < 2U)
    {
        return 0;
    }

    if (prompt != NULL)
    {
        (void)printf("%s", prompt);
    }

    while (1)
    {
        ch = app_getch();
        if (ch == '\r' || ch == '\n')
        {
            break;
        }

        if (ch == 8 || ch == 127)
        {
            if (i > 0U)
            {
                i--;
                (void)printf("\b \b");
            }
            continue;
        }

        if (isprint((unsigned char)ch) && i < size - 1U)
        {
            buffer[i++] = (char)ch;
            (void)printf("*");
        }
    }

    buffer[i] = '\0';
    (void)printf("\n");
    return 1;
}

int read_int(const char *prompt, int *value)
{
    char line[64];
    char *endptr;
    long parsed;

    if (value == NULL)
    {
        return 0;
    }

    if (!read_line(prompt, line, sizeof(line)))
    {
        return 0;
    }

    errno = 0;
    parsed = strtol(line, &endptr, 10);
    if (errno != 0 || endptr == line || *endptr != '\0')
    {
        return 0;
    }

    if (parsed < -2147483647L - 1L || parsed > 2147483647L)
    {
        return 0;
    }

    *value = (int)parsed;
    return 1;
}

void trim_whitespace(char *value)
{
    size_t start;
    size_t end;
    size_t i;

    if (value == NULL)
    {
        return;
    }

    start = 0U;
    while (value[start] != '\0' && isspace((unsigned char)value[start]))
    {
        start++;
    }

    end = strlen(value);
    while (end > start && isspace((unsigned char)value[end - 1U]))
    {
        end--;
    }

    if (start > 0U)
    {
        for (i = start; i <= end; i++)
        {
            value[i - start] = value[i];
        }
    }
    else
    {
        value[end] = '\0';
    }

    if (start > 0U)
    {
        value[end - start] = '\0';
    }
}

void sanitize_field(char *value)
{
    size_t i;

    if (value == NULL)
    {
        return;
    }

    trim_whitespace(value);

    for (i = 0U; value[i] != '\0'; i++)
    {
        if (value[i] == '|' || value[i] == '\n' || value[i] == '\r' || value[i] == '\t')
        {
            value[i] = ' ';
        }
    }
}

void generate_id(char out[VELOCE_ID_LEN])
{
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t i;

    seed_rng_once();
    for (i = 0U; i < VELOCE_ID_LEN - 1U; i++)
    {
        out[i] = chars[(size_t)(rand() % (int)(sizeof(chars) - 1U))];
    }

    out[VELOCE_ID_LEN - 1U] = '\0';
}

void now_timestamp(char out[VELOCE_TIMESTAMP_LEN])
{
    time_t now;
    struct tm tm_info;

    now = time(NULL);
#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif
    (void)strftime(out, VELOCE_TIMESTAMP_LEN, "%Y-%m-%d %H:%M:%S", &tm_info);
}

int read_text_file(const char *path, char **content, size_t *len)
{
    FILE *fp;
    long size;
    size_t read_size;
    char *buf;

    if (path == NULL || content == NULL || len == NULL)
    {
        return -1;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }

    size = ftell(fp);
    if (size < 0)
    {
        fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return -1;
    }

    buf = (char *)malloc((size_t)size + 1U);
    if (buf == NULL)
    {
        fclose(fp);
        return -1;
    }

    read_size = fread(buf, 1U, (size_t)size, fp);
    fclose(fp);

    if (read_size != (size_t)size)
    {
        free(buf);
        return -1;
    }

    buf[size] = '\0';
    *content = buf;
    *len = (size_t)size;
    return 0;
}

int write_text_file(const char *path, const char *content, size_t len)
{
    FILE *fp;

    if (path == NULL)
    {
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        return -1;
    }

    if (len > 0U && content != NULL)
    {
        if (fwrite(content, 1U, len, fp) != len)
        {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int copy_text_file(const char *src, const char *dst)
{
    char *content;
    size_t len;
    int rc;

    if (read_text_file(src, &content, &len) != 0)
    {
        return -1;
    }

    rc = write_text_file(dst, content, len);
    free(content);
    return rc;
}

/* Minimal SHA-256 implementation for portable credential hashing. */
typedef struct
{
    uint8_t data[64];
    uint32_t datalen;
    uint32_t state[8];
    uint64_t bitlen;
} SHA256_CTX;

static const uint32_t k256[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[])
{
    uint32_t m[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t i;
    uint32_t j;
    uint32_t t1;
    uint32_t t2;

    for (i = 0U, j = 0U; i < 16U; i++, j += 4U)
    {
        m[i] = ((uint32_t)data[j] << 24U) | ((uint32_t)data[j + 1U] << 16U) |
               ((uint32_t)data[j + 2U] << 8U) | ((uint32_t)data[j + 3U]);
    }

    for (; i < 64U; i++)
    {
        m[i] = SIG1(m[i - 2U]) + m[i - 7U] + SIG0(m[i - 15U]) + m[i - 16U];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0U; i < 64U; i++)
    {
        t1 = h + EP1(e) + CH(e, f, g) + k256[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->datalen = 0U;
    ctx->bitlen = 0U;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len)
{
    size_t i;

    for (i = 0U; i < len; i++)
    {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64U)
        {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512U;
            ctx->datalen = 0U;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[])
{
    uint32_t i;

    i = ctx->datalen;

    if (ctx->datalen < 56U)
    {
        ctx->data[i++] = 0x80U;
        while (i < 56U)
        {
            ctx->data[i++] = 0x00U;
        }
    }
    else
    {
        ctx->data[i++] = 0x80U;
        while (i < 64U)
        {
            ctx->data[i++] = 0x00U;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56U);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8U;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8U);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16U);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24U);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32U);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40U);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48U);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56U);
    sha256_transform(ctx, ctx->data);

    for (i = 0U; i < 4U; i++)
    {
        hash[i] = (uint8_t)((ctx->state[0] >> (24U - i * 8U)) & 0xffU);
        hash[i + 4U] = (uint8_t)((ctx->state[1] >> (24U - i * 8U)) & 0xffU);
        hash[i + 8U] = (uint8_t)((ctx->state[2] >> (24U - i * 8U)) & 0xffU);
        hash[i + 12U] = (uint8_t)((ctx->state[3] >> (24U - i * 8U)) & 0xffU);
        hash[i + 16U] = (uint8_t)((ctx->state[4] >> (24U - i * 8U)) & 0xffU);
        hash[i + 20U] = (uint8_t)((ctx->state[5] >> (24U - i * 8U)) & 0xffU);
        hash[i + 24U] = (uint8_t)((ctx->state[6] >> (24U - i * 8U)) & 0xffU);
        hash[i + 28U] = (uint8_t)((ctx->state[7] >> (24U - i * 8U)) & 0xffU);
    }
}

void hash_secret(const char *secret, const char *salt, char out[VELOCE_HASH_HEX_LEN])
{
    SHA256_CTX ctx;
    uint8_t digest[32];
    static const char hex[] = "0123456789abcdef";
    size_t i;

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)secret, strlen(secret));
    sha256_update(&ctx, (const uint8_t *)":", 1U);
    sha256_update(&ctx, (const uint8_t *)salt, strlen(salt));
    sha256_final(&ctx, digest);

    for (i = 0U; i < 32U; i++)
    {
        out[i * 2U] = hex[digest[i] >> 4U];
        out[i * 2U + 1U] = hex[digest[i] & 0x0FU];
    }

    out[64] = '\0';
}

void load(void)
{
    app_clear_screen();
    (void)printf("\n\n");
    (void)printf(":::     ::: :::::::::: :::         ::::::::   ::::::::  :::::::::: \n");
    app_sleep_ms(60U);
    (void)printf(":+:     :+: :+:        :+:        :+:    :+: :+:    :+: :+:        \n");
    app_sleep_ms(60U);
    (void)printf("+:+     +:+ +:+        +:+        +:+    +:+ +:+        +:+        \n");
    app_sleep_ms(60U);
    (void)printf("+#+     +:+ +#++:++#   +#+        +#+    +:+ +#+        +#++:++#   \n");
    app_sleep_ms(60U);
    (void)printf(" +#+   +#+  +#+        +#+        +#+    +#+ +#+        +#+        \n");
    app_sleep_ms(60U);
    (void)printf("  #+#+#+#   #+#        #+#        #+#    #+# #+#    #+# #+#        \n");
    app_sleep_ms(60U);
    (void)printf("    ###     ########## ##########  ########   ########  ##########\n\n");
    app_pause("Press any key to continue...");
}

