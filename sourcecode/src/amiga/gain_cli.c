/* PORT38 FIX1 gain parsing: portable between GCC m68k and Linux. */
#include "gain_cli.h"
#include <string.h>

int lite_gain_parse(const char *arg, struct lite_gain_option *out)
{
    const char *p;
    int whole=0, decimal=0, gain;
    struct lite_gain_option tmp;
    if (!arg || !out || strncmp(arg,"GAIN=",5)!=0) return -1;
    p=arg+5;
    if (strcmp(p,"AUTO")==0 || strcmp(p,"auto")==0) {
        tmp.manual=0;
        tmp.tenths_db=0;
        *out=tmp;
        return 0;
    }
    if (*p<'0' || *p>'9') return -1;
    while (*p>='0' && *p<='9') {
        whole=whole*10 + (*p-'0');
        if (whole>49) return -1;
        ++p;
    }
    if (*p=='.') {
        ++p;
        if (*p<'0' || *p>'9') return -1;
        decimal=*p-'0';
        ++p;
    }
    if (*p!='\0') return -1;
    gain=whole*10+decimal;
    if (gain>496) return -1;
    tmp.manual=1;
    tmp.tenths_db=gain;
    *out=tmp;
    return 0;
}
