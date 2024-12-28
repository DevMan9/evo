#include <stdlib.h>

#include "sentinel.h"

int main(int argc, char** argv){
 if(argc < 2){
  return 0;
 }
 
 int loops = 1; 
 if(argc > 2){
  loops = atoi(argv[2]);
 }

 run(argv[1], loops);
 
 return 0;
}
