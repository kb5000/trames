// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary Xlib drawing + server readback. The separate native texture oracle
// checks the presented image on Apple; this checks original glamor's results.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

static void draw(Display *d, Window w, GC gc, int width, int height) {
    Pixmap p = XCreatePixmap(d, w, width, height, DefaultDepth(d, DefaultScreen(d)));
    for (int y=0; y<2; ++y) for (int x=0; x<2; ++x) {
        unsigned long color = (y ? 0 : 0x00ff00) | (x ? 0xff0000 : 0);
        int left=x*width/2, top=y*height/2;
        XSetForeground(d, gc, color);
        XFillRectangle(d, p, gc, left, top, (x+1)*width/2-left, (y+1)*height/2-top);
    }
    XCopyArea(d, p, w, gc, 0, 0, width, height, 0, 0);
    XFreePixmap(d, p);
    XImage *image=XGetImage(d, w, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) { fputs("XGetImage failed\n",stderr); exit(2); }
    for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
        unsigned long expected=(y<height/2 ? 0x00ff00 : 0) | (x>=width/2 ? 0xff0000 : 0);
        if ((XGetPixel(image,x,y)&0xffffff)!=expected) {
            fprintf(stderr,"X11 pixels incorrect at %d,%d\n",x,y);exit(3);
        }
    }
    XDestroyImage(image);
    printf("x11-pixels-ok: %dx%d %d pixels\n",width,height,width*height);fflush(stdout);
}
int main(void) {
    Display *d=XOpenDisplay(NULL);
    if (!d) return 1;
    Window w=XCreateSimpleWindow(d,DefaultRootWindow(d),0,0,128,96,0,0,0);
    GC gc=XCreateGC(d,w,0,NULL);
    Atom close=XInternAtom(d,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(d,w,&close,1);
    XStoreName(d,w,"Trames-X11-pixels");
    XSelectInput(d,w,ExposureMask|StructureNotifyMask);
    XMapWindow(d,w);XFlush(d);
    int width=128,height=96;
    for (;;) {
        XEvent e;XNextEvent(d,&e);
        if (e.type==ClientMessage && (Atom)e.xclient.data.l[0]==close) break;
        if (e.type==ConfigureNotify) { width=e.xconfigure.width;height=e.xconfigure.height; }
        if ((e.type==Expose && !e.xexpose.count) || e.type==ConfigureNotify)
            draw(d,w,gc,width,height);
    }
    XFreeGC(d,gc);XDestroyWindow(d,w);XCloseDisplay(d);return 0;
}
