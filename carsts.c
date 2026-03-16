#include <stdio.h>
#include <windows.h>
#include <tchar.h> // 适配宽字符/多字节
#include <string.h>  // 新增：strcpy必需的头文件

#define EFLIFSD 99

#define DATA 10

#define MAX(a, b) (a > b ? a : b)


#ifdef DATA
    #undef DATA
    #define DATA 20
#endif


typedef struct {
    int id;
    char king[10];
    bool bods;
    void *p;
} sppa;



typedef struct {
    int id;
    char name[256];
    char age[20];
} insData;

union unionData {
    int id;
    char kon[4];
    int ppt;
};

void miax(insData *data) {
    if (data == NULL) {
        printf("为NULL，没有 %s");
    };
    printf("id: %d\n", data->id);
    printf("name: %s\n", data->name);
    printf("age: %s\n", data->age);
};

int main () {

    insData data = {1, "李", "男"};
    insData *datas;
    sppa spa;
    union unionData unis;

    strcpy(unis.kon, "溜");
    printf("union-kon %s\n", unis.kon);
    unis.id = 12;
    unis.ppt = 2;

    printf("union-ppt %d\n", unis.ppt);
    printf("union-id %d\n", unis.id);
    
    miax(&data);

    printf("*datas %p\n", (*datas).name);
    printf("*datas %p\n", datas->name);
    printf("结构体 spa 大小为: %zu 字节\n", sizeof(spa));
    printf("spa %d\n", spa.id);
    spa.id = 1;
    strcpy(spa.king, "哈");
    spa.bods = false;
    spa.p = data.name;

    printf("%d %s %p %p\n", spa.id, spa.king, spa.bods, spa.p);
    printf("%d %s %s\n",data.id, data.name, data.age);
    printf("DATA %d\n", DATA);
    printf("EFLIFSD %d\n", EFLIFSD);
    printf("EFLIFSD %d\n", MAX(2,9));
    
    int ins = 10;
    int *p;
    p = &ins;

    printf("app： %d\n", *p);

    FILE *fp = fopen("123.txt", "r");
    FILE *fs = fopen("123.txt", "w+b");

    if (fp == NULL) {
        printf("文件打开失败");
    }

    fprintf(fs, "哈哈哈\n");

    fclose(fp);
    system("pause");
    return 0;
}
