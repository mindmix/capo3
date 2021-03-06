/*======================================================== 
** University of Illinois/NCSA 
** Open Source License 
**
** Copyright (C) 2011,The Board of Trustees of the University of 
** Illinois. All rights reserved. 
**
** Developed by: 
**
**    Research Group of Professor Sam King in the Department of Computer 
**    Science The University of Illinois at Urbana-Champaign 
**    http://www.cs.uiuc.edu/homes/kingst/Research.html 
**
** Copyright (C) Sam King
**
** Permission is hereby granted, free of charge, to any person obtaining a 
** copy of this software and associated documentation files (the 
** Software), to deal with the Software without restriction, including 
** without limitation the rights to use, copy, modify, merge, publish, 
** distribute, sublicense, and/or sell copies of the Software, and to 
** permit persons to whom the Software is furnished to do so, subject to 
** the following conditions: 
**
** Redistributions of source code must retain the above copyright notice, 
** this list of conditions and the following disclaimers. 
**
** Redistributions in binary form must reproduce the above copyright 
** notice, this list of conditions and the following disclaimers in the 
** documentation and/or other materials provided with the distribution. 
** Neither the names of Sam King or the University of Illinois, 
** nor the names of its contributors may be used to endorse or promote 
** products derived from this Software without specific prior written 
** permission. 
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
** IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR 
** ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
** SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE SOFTWARE. 
**========================================================== 
*/

#include <stdio.h>
#include <stdlib.h>

#define __USE_GNU

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <asm/unistd.h>

#include "util.h"

void handle_chunk_log(int chunkFd) {
        int replayFd;
        chunk_t chunk;
        int ret;

        replayFd = open("/dev/replay0", O_WRONLY | O_CLOEXEC);
        if(replayFd < 0) {
                fprintf(stderr,"could not open /dev/replay device\n");
                exit(0);
        }
        ret = ioctl(replayFd, REPLAY_IOC_SET_CHUNK_LOG_FD, 0);
        assert(ret == 0);

        while((ret = read(chunkFd, &chunk, sizeof(chunk))) > 0) {
                write_bytes(replayFd, &chunk, sizeof(chunk));
        }

        /*
        while(read_chunk(chunkFd, &chunk)) {
                write_bytes(replayFd, &chunk, sizeof(chunk));
        }
        */
}

int main(int argc, char *argv[]) {
        unsigned char buf[4096];
        int replayFd, ret, status;
        replay_header_t header;
        struct execve_data *e;
        int chunkFd = -1;

        if(argc > 2) {
                fprintf(stderr, "Usage %s: [chunk_log] < replay_log\n", argv[0]);
                return 0;
        }

        replayFd = open("/dev/replay0", O_WRONLY | O_CLOEXEC);
        if(replayFd < 0) {
                fprintf(stderr,"could not open /dev/replay device\n");
                return 0;
        }
        ret = ioctl(replayFd, REPLAY_IOC_RESET_SPHERE, 0);
        assert(ret == 0);

        if(argc == 2) {
                chunkFd = open(argv[1], O_RDONLY);
                if(chunkFd < 0) {
                        fprintf(stderr, "Usage %s: [chunk_log] < replay_log\n", argv[0]);
                        return 0;
                }
                if(fork() == 0) {
                        handle_chunk_log(chunkFd);
                        exit(0);
                }
        }

        ret = read(STDIN_FILENO, &header, sizeof(header));
        assert(ret == sizeof(header));
    
        e = readExecveData();
        if(chunkFd < 0) {
                startChild(replayFd, e->argv, e->envp, START_REPLAY);
        } else {
                close(chunkFd);
                chunkFd = -1;
                startChild(replayFd, e->argv, e->envp, START_CHUNKED_REPLAY);
        }

        // before we start pushing data down to the kernel, burn off the ioctl entries
        // at the beginning of the stream until we get the return from the first execve
        while((ret = read(STDIN_FILENO, &header, sizeof(header))) > 0) {
                assert(ret == sizeof(header));
                if((header.type == syscall_exit_event) && 
                   (regs_syscallno(&header.regs) == __NR_execve)) {
                        write_bytes(replayFd, &header, sizeof(header));
                        break;
                } else if(header.type == copy_to_user_event) {
                        readBuffer();
                } else if((header.type != syscall_exit_event) && (header.type != syscall_enter_event)) {
                        write_bytes(replayFd, &header, sizeof(header));
                }
        }

        assert((header.type == syscall_exit_event) && 
               (regs_syscallno(&header.regs) == __NR_execve));

        while((ret = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
                write_bytes(replayFd, buf, ret);
        } 

        while(wait(&status) != -1)
                ;

        return 1;
}
