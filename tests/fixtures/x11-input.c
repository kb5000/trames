// SPDX-License-Identifier: GPL-3.0-or-later
// Original Xlib XIM/selection client. No Trames native-call ABI in this test.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save(const char *path,const unsigned char *bytes,size_t len,const char *mode) {
    FILE *f=fopen(path,mode);if(!f)abort();if(fwrite(bytes,1,len,f)!=len)abort();fclose(f);
}
int main(void) {
    setlocale(LC_ALL,"");XSetLocaleModifiers("");
    Display *d=XOpenDisplay(NULL);if(!d)return 1;
    Window w=XCreateSimpleWindow(d,DefaultRootWindow(d),0,0,160,120,0,0,0x306030);
    XStoreName(d,w,"Trames-X11-input");
    XSelectInput(d,w,ExposureMask|FocusChangeMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|PropertyChangeMask);
    Atom close=XInternAtom(d,"WM_DELETE_WINDOW",False);XSetWMProtocols(d,w,&close,1);
    Atom clip=XInternAtom(d,"CLIPBOARD",False),utf8=XInternAtom(d,"UTF8_STRING",False);
    Atom targets=XInternAtom(d,"TARGETS",False),property=XInternAtom(d,"TRAMES_TEST_PASTE",False),incr=XInternAtom(d,"INCR",False);
    XIM im=XOpenIM(d,NULL,NULL,NULL);if(!im){fputs("XOpenIM failed\n",stderr);return 2;}
    XIC ic=XCreateIC(im,XNInputStyle,XIMPreeditNothing|XIMStatusNothing,XNClientWindow,w,XNFocusWindow,w,NULL);
    if(!ic)return 3;
    XMapWindow(d,w);XFlush(d);
    char large[9001];for(int i=0;i<3000;i++)memcpy(large+i*3,"段",3);large[9000]=0;
    const char *copied="X11 复制 🧪";int incremental=0,offset=0;Window receiver=0;Atom receiver_property=0;
    puts("x11-input-ready");fflush(stdout);
    for(;;) {
        XEvent e;XNextEvent(d,&e);
        if(XFilterEvent(&e,None))continue;
        if(e.type==FocusIn){XSetICFocus(ic);puts("xim-focused");fflush(stdout);}
        if(e.type==FocusOut)XUnsetICFocus(ic);
        if(e.type==ClientMessage && (Atom)e.xclient.data.l[0]==close)break;
        if(e.type==ButtonPress && e.xbutton.button==3) {
            copied="X11 菜单复制 🧪";incremental=0;XSetSelectionOwner(d,clip,w,CurrentTime);
        }
        if(e.type==KeyPress) {
            char text[4096];KeySym key=0;Status status=0;
            int n=Xutf8LookupString(ic,&e.xkey,text,sizeof(text),&key,&status);
            if(status==XBufferOverflow)return 4;
            if(key==XK_F5 || (key==XK_c && (e.xkey.state&ControlMask))){copied="X11 复制 🧪";incremental=0;XSetSelectionOwner(d,clip,w,CurrentTime);}
            else if(key==XK_F7){copied=large;incremental=1;XSetSelectionOwner(d,clip,w,CurrentTime);}
            else if(key==XK_F6)XConvertSelection(d,clip,utf8,property,w,CurrentTime);
            else if(key==XK_F8){char *pending=Xutf8ResetIC(ic);if(pending)XFree(pending);}
            else if(key==XK_Left)save("/tmp/x11-events",(unsigned char*)"<left>",6,"ab");
            else if(n>0){save("/tmp/x11-committed",(unsigned char*)text,(size_t)n,"ab");save("/tmp/x11-events",(unsigned char*)text,(size_t)n,"ab");}
        }
        if(e.type==SelectionRequest) {
            XSelectionRequestEvent *r=&e.xselectionrequest;
            Atom p=r->property?r->property:r->target;
            XSelectionEvent reply={.type=SelectionNotify,.display=d,.requestor=r->requestor,
                .selection=r->selection,.target=r->target,.time=r->time,.property=p};
            if(r->target==targets) {
                Atom values[]={targets,utf8,XA_STRING};
                XChangeProperty(d,r->requestor,p,XA_ATOM,32,PropModeReplace,(unsigned char*)values,3);
            } else if(r->target==utf8 || r->target==XA_STRING) {
                if(incremental) {
                    unsigned long count=strlen(copied);receiver=r->requestor;receiver_property=p;offset=0;
                    XSelectInput(d,receiver,PropertyChangeMask);
                    XChangeProperty(d,receiver,p,incr,32,PropModeReplace,(unsigned char*)&count,1);
                } else XChangeProperty(d,r->requestor,p,r->target,8,PropModeReplace,(unsigned char*)copied,(int)strlen(copied));
            } else reply.property=None;
            XSendEvent(d,r->requestor,False,0,(XEvent*)&reply);
        }
        if(e.type==PropertyNotify && e.xproperty.window==receiver && e.xproperty.atom==receiver_property && e.xproperty.state==PropertyDelete) {
            int remaining=(int)strlen(copied)-offset,n=remaining>1024?1024:remaining;
            XChangeProperty(d,receiver,receiver_property,utf8,8,PropModeReplace,(unsigned char*)copied+offset,n);
            offset+=n;if(!n)receiver=0;
        }
        if(e.type==SelectionNotify && e.xselection.property) {
            Atom type;int format;unsigned long count,after;unsigned char *bytes=NULL;
            XGetWindowProperty(d,w,property,0,4096,True,AnyPropertyType,&type,&format,&count,&after,&bytes);
            if(format!=8 || after)return 5;
            save("/tmp/x11-pasted",bytes,count,"wb");XFree(bytes);
            puts("x11-paste-done");fflush(stdout);
        }
        XFlush(d);
    }
    XDestroyIC(ic);XCloseIM(im);XDestroyWindow(d,w);XCloseDisplay(d);return 0;
}
