#include "pe_exports.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    *size = fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    size_t a_size = 0, b_size = 0;
    uint8_t *a = slurp(argv[1], &a_size), *b = slurp(argv[2], &b_size);
    int fails = 0;
    fails += !a || !pe_exports_name(a, a_size, "SwoftyAddonRegister");
    fails += !a || !pe_exports_name(a, a_size, "SwoftyAddonInit");
    fails += !b || pe_exports_name(b, b_size, "SwoftyAddonRegister");
    fails += !b || !pe_exports_name(b, b_size, "SwoftyAddonRegisterX");
    fails += pe_exports_name((const uint8_t *)"MZ", 2, "SwoftyAddonRegister");
    for (size_t cut = 0; a && cut < a_size; cut += 97)
        pe_exports_name(a, cut, "SwoftyAddonRegister");
    printf("pe export scan: %s\n", fails ? "FAILED" : "ok (export found, absent, near-miss name, truncated images)");
    free(a);
    free(b);
    return fails ? 1 : 0;
}
