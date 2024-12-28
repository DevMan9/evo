#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define HEXMAP "0123456789ABCDEF"
#define NOP() __asm__ volatile("nop");
#define NOP2() NOP() NOP()
#define NOP4() NOP2() NOP2()
#define NOP8() NOP4() NOP4()
#define NOP16() NOP8() NOP8()
#define NOP32() NOP16() NOP16()
#define NOP64() NOP32() NOP32()
#define NOP128() NOP64() NOP64()
#define NOP256() NOP128() NOP128()
#define NOP512() NOP256() NOP256()
#define NOP1024() NOP512() NOP512()

void reproduce(char* parent_name){
 //Seed random from nanotime
 struct timespec t;
 clock_gettime(CLOCK_MONOTONIC, &t);
 srand(t.tv_nsec);

 //Generate baby name
 char name[9];
 for(size_t i = 0; i < 8; i++){
  name[i] = HEXMAP[rand() & 0xf];
 }
 name[8] = '\0';

 //Open parent file
 int p_f = open(parent_name, O_RDONLY);
 if(p_f == -1){
  exit(1);
 }

 //Get size of parent file
 struct stat p_s;
 if(fstat(p_f, &p_s) == -1){
  exit(1);
 }

 //Allocate space for the parent data
 char* src = malloc(p_s.st_size);
 if(src == 0){
  exit(1);
 }

 //Read parent file
 read(p_f, src, p_s.st_size);

 //Create a copy of the parent source
 //We will do this carefully to allow for mutation
 unsigned char bits = 10;
 size_t page_size = 1 << bits;
 size_t mask = page_size - 1;
 float radiation = 0.0f;

 void** head = malloc(2 * sizeof (*head));
 void** tmp = head;

 size_t new_size = 0;
 size_t last_page = -1;

 for(size_t i = 0; i < p_s.st_size; new_size++){
  if((new_size & mask) == 0 && last_page != new_size){
   tmp[1] = malloc(2 * sizeof (*head));
   tmp = tmp[1];
   tmp[0] = malloc(page_size);
   last_page = new_size;
  }

  char mutation = 0;
  if((((float)rand()) / RAND_MAX) < radiation){
   mutation = (rand() % 3) + 1;
  }

  switch(mutation){
   //Clean copy
   default:
   case 0:
    ((char*)(tmp[0]))[new_size & mask] = src[i];
    goto increment_source;

   //Change
   case 1:
    ((char*)(tmp[0]))[new_size & mask] = (unsigned char)rand();
    goto increment_source;

   //Insert
   case 2:
    ((char*)(tmp[0]))[new_size & mask] = (unsigned char)rand();
    break;

   //Delete
   case 3:
    new_size--;
    goto increment_source;

   increment_source:
    i++;
    break;
  }
 }

 char* chi = malloc(new_size);
 tmp = head[1];
 free(head);
 for(size_t i = 0; i < new_size; i += page_size){
  memcpy(&chi[i], tmp[0], page_size);
  void** next = tmp[1];
  free(tmp);
  tmp = next;
 }

 free(src);
 
 NOP1024();
 
 int baby = open(name, O_CREAT | O_WRONLY, 0755);

 if(write(baby, chi, new_size) == -1){
  unlink(name);
 }

 close(baby);
 exit(0);
}

void run(){
 size_t ram_size = 256;
 char* buf = malloc(ram_size);
 ssize_t bytes_read;
 while(1){
  bytes_read = read(STDIN_FILENO, buf, ram_size);
  NOP1024();
  write(STDOUT_FILENO, buf + 2, 1);
 }
 free(buf);
}

int main(int argc, char** argv){
 if(argc < 2){
  run();
 }else{
  reproduce(&argv[0][2]);
 }

 return 0;
}
