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

#include <iostream>

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

#include "util.h"

using namespace std;


int main(void) {
    unsigned char buf[4096];
    int replayFd, ret, bytesWritten, status, len;
    replay_header_t header;
    struct execve_data *e;

    replayFd = open("/dev/replay0", O_WRONLY | O_CLOEXEC);
    if(replayFd < 0) {
        cerr << "could not open /dev/replay device" << endl;
        return 1;
    }
    ret = ioctl(replayFd, REPLAY_IOC_RESET_SPHERE, 0);
    assert(ret == 0);

    ret = read(STDIN_FILENO, &header, sizeof(header));
    assert(ret == sizeof(header));
    
    e = readExecveData();
    startChild(replayFd, e->argv, e->envp, 0);

    while((ret = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        bytesWritten = 0;
        len = ret;
        while(bytesWritten < len) {
            ret = write(replayFd, buf+bytesWritten, len-bytesWritten);
            assert(ret > 0);
            bytesWritten += ret;
        }
    }

    while(wait(&status) != -1)
        ;

    return 0;
}