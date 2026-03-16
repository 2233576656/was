
#include "include/common.h"


int main() {
    FILE *fq = fopen("text.txt", "w");

    if (fq == NULL) {
        printf("fq - 无内容");
    };
    fputc('3', fq);
    fputs("哈哈哈", fq);

    fclose(fq);

    return 0;
}