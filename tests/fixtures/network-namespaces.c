// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int local(void) { int fd=socket(AF_UNIX,SOCK_STREAM,0); assert(fd>=0); return fd; }
static int tcp(void) { int fd=socket(AF_INET,SOCK_STREAM,0); assert(fd>=0); return fd; }
static void sysctl(const char *name,const char *value) {
    int fd=open(name,O_WRONLY|O_TRUNC); assert(fd>=0);
    assert(write(fd,value,strlen(value))==(ssize_t)strlen(value)); close(fd);
}
static void nested_proc(ino_t original) {
    pid_t child=fork(); assert(child>=0);
    if(!child) {
        assert(unshare(CLONE_NEWNS|CLONE_NEWNET|CLONE_NEWIPC|CLONE_NEWPID)==0);
        pid_t init=fork(); assert(init>=0);
        if(!init) {
            assert(getpid()==1);
            assert(mount("proc","/proc","proc",MS_NOSUID|MS_NOEXEC|MS_NODEV,NULL)==0);
            int fd=open("/proc/self/ns/net",O_RDONLY); assert(fd>=0);
            struct stat st; assert(fstat(fd,&st)==0 && st.st_ino!=original);
            char label[100]={0}; assert(readlink("/proc/self/ns/net",label,sizeof(label)-1)>0);
            unsigned long inode; assert(sscanf(label,"net:[%lu]",&inode)==1);
            if(inode!=st.st_ino)fprintf(stderr,"nested proc namespace readlink=%lu fstat=%lu\n",inode,(unsigned long)st.st_ino);
            assert(inode==st.st_ino); close(fd); _exit(0);
        }
        int status; assert(waitpid(init,&status,0)==init && WIFEXITED(status) && WEXITSTATUS(status)==0); _exit(0);
    }
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(void) {
    assert(mkdir("/tmp",0777)==0 || errno==EEXIST);
    int original=open("/proc/self/ns/net",O_RDONLY); assert(original>=0);
    struct stat old; assert(fstat(original,&old)==0);
    nested_proc(old.st_ino);
    int server=tcp(),old_client=tcp();
    struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(bind(server,(void*)&address,sizeof(address))==0); assert(listen(server,2)==0);
    socklen_t length=sizeof(address); assert(getsockname(server,(void*)&address,&length)==0);
    struct sockaddr_un abstract={.sun_family=AF_UNIX};
    memcpy(abstract.sun_path+1,"namespace",9);
    socklen_t abstract_size=sizeof(sa_family_t)+10;
    int a=local(),old_abstract=local(); assert(bind(a,(void*)&abstract,abstract_size)==0); assert(listen(a,2)==0);
    struct sockaddr_un file={.sun_family=AF_UNIX};
    snprintf(file.sun_path,sizeof(file.sun_path),"/tmp/netns-%ld.sock",(long)getpid());
    int named=local(); assert(bind(named,(void*)&file,sizeof(file))==0); assert(listen(named,2)==0);
    assert(unshare(CLONE_NEWNET|CLONE_NEWIPC|CLONE_NEWCGROUP)==0);
    int own=open("/proc/self/ns/net",O_RDONLY); assert(own>=0);
    struct stat current; assert(fstat(own,&current)==0 && current.st_ino!=old.st_ino);
    int control=socket(AF_INET,SOCK_DGRAM,0); assert(control>=0);
    struct sockaddr_in datagram={.sin_family=AF_INET};
    assert(bind(control,(void*)&datagram,sizeof(datagram))==0);
    socklen_t datagram_size=sizeof(datagram);
    assert(getsockname(control,(void*)&datagram,&datagram_size)==0);
    struct ifreq interface={0}; strcpy(interface.ifr_name,"lo");
    assert(ioctl(control,SIOCGIFFLAGS,&interface)==0 && !(interface.ifr_flags & IFF_UP));
    strcpy(interface.ifr_name,"eth0"); errno=0;
    assert(ioctl(control,SIOCGIFFLAGS,&interface)==-1 && errno==ENODEV);
    strcpy(interface.ifr_name,"lo"); interface.ifr_flags=IFF_UP;
    assert(ioctl(control,SIOCSIFFLAGS,&interface)==0);
    datagram.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int sender=socket(AF_INET,SOCK_DGRAM,0); assert(sender>=0);
    assert(sendto(sender,"u",1,0,(void*)&datagram,sizeof(datagram))==1);
    char message; assert(read(control,&message,1)==1 && message=='u');
    int isolated=tcp(); assert(bind(isolated,(void*)&address,sizeof(address))==0); assert(listen(isolated,2)==0);
    int client=tcp(); assert(connect(client,(void*)&address,sizeof(address))==0);
    int accepted=accept(isolated,NULL,NULL); assert(accepted>=0);
    assert(write(client,"private",7)==7); char bytes[16]; assert(read(accepted,bytes,7)==7 && !memcmp(bytes,"private",7));
    assert(connect(old_client,(void*)&address,sizeof(address))==0);
    int accepted_old=accept(server,NULL,NULL); assert(accepted_old>=0);
    int b=local(); errno=0;
    assert(connect(b,(void*)&abstract,abstract_size)==-1 && errno==ECONNREFUSED);
    assert(bind(b,(void*)&abstract,abstract_size)==0); assert(listen(b,2)==0);
    assert(connect(old_abstract,(void*)&abstract,abstract_size)==0);
    int accepted_abstract=accept(a,NULL,NULL); assert(accepted_abstract>=0);
    int file_client=local(); assert(connect(file_client,(void*)&file,sizeof(file))==0);
    int accepted_named=accept(named,NULL,NULL); assert(accepted_named>=0);
    struct sockaddr_in remote={.sin_family=AF_INET,.sin_port=htons(9999)};
    assert(inet_pton(AF_INET,"198.51.100.1",&remote.sin_addr)==1);
    int udp=socket(AF_INET,SOCK_DGRAM,0); assert(udp>=0); errno=0;
    assert(sendto(udp,"x",1,0,(void*)&remote,sizeof(remote))==-1 && errno==ENETUNREACH);
    errno=0; assert(socket(AF_INET,SOCK_DGRAM,IPPROTO_ICMP)==-1 && errno==EACCES);
    sysctl("/proc/sys/net/ipv4/ping_group_range","0 2147483647\n");
    int ping=socket(AF_INET,SOCK_DGRAM,IPPROTO_ICMP); assert(ping>=0); close(ping);
    sysctl("/proc/sys/net/ipv4/ip_unprivileged_port_start","0\n");
    assert(setns(original,CLONE_NEWNET)==0);
    int again=open("/proc/self/ns/net",O_RDONLY); assert(again>=0);
    assert(fstat(again,&current)==0 && current.st_ino==old.st_ino);
    int handles[]={original,own,control,sender,server,old_client,a,old_abstract,named,isolated,client,accepted,accepted_old,b,accepted_abstract,file_client,accepted_named,udp,again};
    for(unsigned i=0;i<sizeof(handles)/sizeof(handles[0]);++i)close(handles[i]);
    unlink(file.sun_path);
    puts("network namespaces: private TCP/UDP/Unix, old FDs, pathname reachability, lo, sysctls and setns: ok");
    return 0;
}
