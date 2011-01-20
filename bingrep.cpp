#include <stdio.h>
#include <string.h>
#include "bin64.h"

int main (int argn, char** args) {
    int lr;
    unsigned long long of;
    sscanf(args[1],"%i,%lli",&lr,&of);
    bin_t target(lr,of);
    char line[1024];
    while (gets(line)) {
        char* br = strchr(line,'(');
        if (br && 2==sscanf(br,"(%i,%lli)",&lr,&of)) {
            bin_t found(lr,of);
            if ( found.within(target) || target.within(found))
                printf("%s\n",line);
        }
    }
    return 0;
}
