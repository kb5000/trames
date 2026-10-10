// SPDX-License-Identifier: GPL-3.0-or-later
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void show(const char *name,double x) {
    uint64_t bits;memcpy(&bits,&x,8);printf("%s %016llx %x\n",name,(unsigned long long)bits,fetestexcept(FE_ALL_EXCEPT));feclearexcept(FE_ALL_EXCEPT);
}
int main(void) {
    const double values[]={0.,-0.,0.1,1.,2.,-1.,1e-300,1e300,INFINITY,-INFINITY,NAN};
    for (unsigned i=0;i<sizeof values/sizeof *values;i++) {
        volatile double x=values[i];
        show("sqrt",sqrt(x));show("sin",sin(x));show("cos",cos(x));show("exp",exp(x));show("log",log(x));show("pow",pow(x,1.25));show("fma",fma(x,x,-1.));show("hypot",hypot(x,3.));
        printf("format %.17g %.5e %.2f\n",x,x,x);
    }
    int rounds[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for (unsigned i=0;i<4;i++) {if(fesetround(rounds[i]))return 1;volatile double x=1.;volatile double y=0x1p-53;show("round",x+y);show("rint",rint(1.5));}
    return 0;
}
