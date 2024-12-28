#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <poll.h>
#include <string.h>
#include <sys/wait.h>

#include "sentinel.h"

//This variable controls how much sentinel's built in mutator will will mutate each file it tests.
//0 mean no muation,
//1 means 100% mutation.
float radiation = 0.00001f;

//This variable is the desired population size (the number of FILES we WANT to see in the specificed directory.
size_t desired_population = 1024;

//This variable is the desired sample size per test.
size_t desired_sample_size = 128;

//Amount of mercy to be given to each specimen.
//If a large portion of the population has a success_rate of 0
//Then none of them would be able to reproduce.
//This mercy gives each specimen a base chance to reproduce.
//The higher the value, the less that success_rate matters.
float mercy = 0.05f;
//Similarly, unlucky gives each specimen a base chance to be killed.
float unlucky = 0.001f;

pid_t my_pid = -1;
int out[2];
int in[2];

typedef struct evo_entry{
 char* name;
 float success_rate;
}Evo_Entry;

int setBlocking(int fd, int will_block){
 int flags = fcntl(fd, F_GETFL, 0);
 if(will_block){
  return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
 }else{
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
 }
}

void clearBuffer(int fd){
 char buf[1024];
 setBlocking(fd, 0);
 while(read(fd, buf, 1024) > 0){
  //Do nothing.
 }
 setBlocking(fd, 1);
}

void mutateFile(char* file_name, char* directory){
 //printf("Mutating %s\n", file_name);
 //Format file path
 size_t full_path_length = strlen(directory) + strlen(file_name) + 2;
 char* formatted_path = malloc(full_path_length);

 snprintf(formatted_path, full_path_length, "%s/%s", directory, file_name);

 //Open
 int f = open(formatted_path, O_RDWR);
 if(f == -1){
  printf("mutateFile: Failed to open %s\n", formatted_path);
  goto free_formatted_path;
 }

 //Measure
 struct stat s;
 if(fstat(f, &s) == -1){
  printf("mutateFile: Failed to measure %s\n", formatted_path);
  goto close_file;
 }

 //Read
 char* source = malloc(s.st_size);
 if(source == 0){
  printf("mutateFile: Failed to allocate space for %s\n", formatted_path);
  goto close_file;
 }

 read(f, source, s.st_size);
 
 //Mutate
 //The funny linked list is back! (Technically for the first time, but I wrote this one second :P)
 unsigned char bits = 10;
 size_t page_size = 1 << bits;
 size_t mask = page_size - 1;
 //This is also a dummy head because it so easily avoids edge cases doing it this way.
 void** head = malloc(2 * sizeof (*head));
 
 void** tmp = head;

 //Seed random
 struct timespec t;
 clock_gettime(CLOCK_MONOTONIC, &t);
 srand(t.tv_nsec);

 size_t new_size = 0;
 size_t last_page = -1;
 for(size_t i = 0; i < s.st_size; new_size++){
  if((new_size & mask) == 0 && last_page!=new_size){
   //printf("New Page!\n");
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
    ((char*)(tmp[0]))[new_size & mask] = source[i];
    goto increment_source;

   //Change
   case 1:
    //printf("Changed byte.\n");
    ((char*)(tmp[0]))[new_size & mask] = (unsigned char)rand();
    goto increment_source;

   //Insert
   case 2:
    //printf("Inserted byte.\n");
    ((char*)(tmp[0]))[new_size & mask] = (unsigned char)rand();
    break;

   //Delete
   case 3:
    //printf("Deleted byte.\n");
    new_size--;
    goto increment_source;

   increment_source:
    i++;
    break;
  }
 }

 char* mutated_buf = malloc(new_size);
 tmp[1] = NULL;
 tmp = head[1];
 //printf("Freeing false head\n");
 free(head);

 //printf("Freeing the rest\n");
 for(size_t i = 0; i < new_size; i += page_size){
  size_t copy_size = page_size;
  if(i + page_size > new_size){
   copy_size = new_size & mask;
  }
  memcpy(&mutated_buf[i], tmp[0], copy_size);
  void** next = tmp[1];
  free(tmp[0]);
  free(tmp);
  tmp = next;
 }

 //printf("Freeing the source\n");
 free(source);

 //Seek
 if(lseek(f, 0, SEEK_SET) == -1) {
  printf("mutateFile: Failed to seek %s\n", formatted_path);
  goto free_mutation;
 } 
 
 //Write
 if(write(f, mutated_buf, new_size) == -1){
  perror("Mutant write failed for some reason...");
  exit(EXIT_FAILURE);
  printf("mutateFile: Failed to write %s\n", formatted_path);
  goto free_mutation;
 }

 //Truncate
 ftruncate(f, new_size);

free_mutation:
 free(mutated_buf);
 //Close
close_file:
 close(f);
free_formatted_path:
 free(formatted_path);

}

int rateFloatCompare(const void* evo_entry, const void* value){
 float rate = (*(Evo_Entry*)evo_entry).success_rate;
 float v = *(float*)value;

 if(v > rate){
  return 1;
 }else if(v < rate){
  return -1;
 }
 
 return 0; 
}

int floatFloatCompare(const void* array, const void* value){
 float a = *(float*)array;
 float v = *(float*)value;
 
 if(v > a){
  return 1;
 }else if(v < a){
  return -1;
 }
 
 return 0;
}

size_t fit(void* array, size_t size_of_element, int (*compare)(const void*, const void*), float value, size_t bottom, size_t top){
 if(top <= bottom){
  return bottom;
 }
 
 size_t mid = (top - bottom) / 2 + bottom;
 size_t mid_plus = mid + 1;
 
 int test = compare(array + size_of_element * mid ,&value);
 int test_plus = compare(array + size_of_element * mid_plus, &value);
 
 if(test_plus >= 0){
  return fit(array, size_of_element, compare, value, mid_plus, top);
 }else if(test < 0){
  return fit(array, size_of_element, compare, value, bottom, mid);
 }
 return mid;
}

//Given:
//char* path			 - the path to a directory.
//char*** specimen_sources	 - a address to string array.  The function intends to return a pointer through this address.
//size_t* specimen_sources_count - a address to a size_t. The function intends to return a value through this address.
//Returns an array of file names of all regular files found at path. 
void getSpecimens(char* path, char*** specimen_sources, size_t* specimen_sources_count){
 //Open the current working directory
 //printf("Don't forget to error check opendir() in getSpecimens()!\n");
 DIR* dp = opendir(path);
 
 //Initialize a dummy head of a linked list.
 //Using a dummy to skip checking some edge cases.
 void** ll_head = malloc(2 * sizeof (*ll_head));
 
 //Initialize a temporary linked list node for appending new nodes to the end of the list.
 void** ll_tmp = ll_head;

 //Initialize a variable to count the entries in the directory.
 *specimen_sources_count = 0;

 //Iterate the directory
 for(struct dirent* entry = readdir(dp); entry != NULL; entry = readdir(dp)){
  //If entry is a regualar file
  if(entry->d_type == DT_REG){
   //Then make a copy of its name.

   //Store the d_namlen + 1. (We want to copy the NULL terminating byte too.)
   unsigned char specimen_name_length = strlen(entry->d_name) + 1;
   
   //Allocate space for the specimen_name.
   char* specimen_name = malloc(specimen_name_length);

   //Copy the name from d_name to specimen_name.
   memcpy(specimen_name, entry->d_name, specimen_name_length);
   
   //Allocate space for a new linked list node and append it to the end of the current linked list node
   //Yes, the head will remain emtpty under these rules.  That's why it's a dummy.  You don't have to NULL check this way.
   ll_tmp[1] = malloc(2 * sizeof (*ll_head));
   
   //Traverse to the newly created node
   ll_tmp = ll_tmp[1];
   
   //Store the specimen's name in the newly created node.
   ll_tmp[0] = specimen_name;

   //Increment the specimen_sources_count.
   (*specimen_sources_count)++;

   //printf("%s\n", specimen_name);
  }
 }
  
 //Close the directory.  We don't need it anymore.
 closedir(dp);
 
 //Ensure NULL termination
 ll_tmp[1] = NULL;

 //Reset ll_tmp to the true head of the linked list.
 //Remember ll_head was a dummy.
 ll_tmp = ll_head[1];

 //printf("Free head\n");
 //Free the dummy head.
 free(ll_head);
 
 //Allocate an array to store all the names in the linked list. 
 *specimen_sources = malloc((*specimen_sources_count) * sizeof(**specimen_sources));
 

 //printf("Free the rest\n");
 //Iterate the linked list.
 //It's a little unorthodox.
 //i is simply keeping track of the index of specimen_sources to store the names in.
 //Otherwise it's just a while(ll_tmp != NULL) loop.
 for(size_t i = 0; ll_tmp != NULL; i++){
  //Store the name in the specimen_sources array.
  (*specimen_sources)[i] = ll_tmp[0];
  
  //Store the next element in the linked list.  We won't have access to it otherwise.
  void** next = ll_tmp[1];
  
  //free the current linked list node.
  free(ll_tmp); 

  //Traverse the linked list.
  ll_tmp = next;
 }

 //We passed by reference so there's nothing left to do.
} 

Evo_Entry* selectSpecimens(char** specimen_sources, size_t specimen_sources_count, size_t select_count){
 struct timespec tp;
 clock_gettime(CLOCK_MONOTONIC, &tp);
 srand(tp.tv_nsec);
 
 Evo_Entry* population = malloc(select_count * sizeof (*population));
  
 size_t* indicies = malloc(specimen_sources_count * sizeof (*indicies));
 for(size_t i = 0; i < specimen_sources_count; i++){
  indicies[i] = i;
 }

 for(size_t i = 0; i < select_count; i++){
  size_t end = specimen_sources_count - i - 1;
  size_t random_index = rand() % (end + 1);

  size_t specimen_index = indicies[random_index];
  indicies[random_index] = indicies[random_index]^indicies[end];
  indicies[end] = indicies[random_index]^indicies[end];
  indicies[random_index] = indicies[random_index]^indicies[end];
  
  population[i].name = specimen_sources[specimen_index];
 }

 free(indicies);
 return population;
}

void test(Evo_Entry* entry, char* path){
 clearBuffer(in[0]);
 clearBuffer(out[0]);

 pid_t process_id = fork();
 if(process_id == 0){
  //"Jailing" the child. Hey, it's better than nothing,
  chdir(path);
  chroot(path);

  //Overwritting STDIN_FILENO with the out[read] pipe so that the sentinel can send us bytes.  
  dup2(out[0], STDIN_FILENO);
  //Same for STDOUT_FILENOE and the in[write] pipe.
  dup2(in[1], STDOUT_FILENO);

  //Formatting name into executable.
  size_t length = strlen(entry->name);
  char* executable = malloc(length + 3);
  executable[0] = '.';
  executable[1] = '/';
  memcpy(&executable[2], entry->name, length + 1);
  
  execve(executable,NULL,NULL);
  exit(EXIT_FAILURE);
 }
 if(process_id == -1){
  return;
 }

 unsigned long success = 0;
 unsigned long tests = 1000;


 for(unsigned int test_number = 0; test_number < tests; test_number++){
  clearBuffer(in[0]);
  clearBuffer(out[0]);

  char send_it[2];
  send_it[0] = rand() % 50;
  send_it[1] = rand() % 50;  

  write(out[1], send_it, 2);
  //printf("Sent: %u, %u\n", send_it[0], send_it[1]);

  char buf[256];
  ssize_t bytes_read = 0;

  struct pollfd pfds[1];  

  pfds[0].fd = in[0];
  pfds[0].events = POLLIN;

  poll(pfds, 1, 200);

  if(pfds[0].revents & POLLIN){
   setBlocking(in[0], 1);
   bytes_read = read(in[0], buf, 256);
   setBlocking(in[0], 0);
  
   if(bytes_read > 0){
    //printf("Sent back: %u,\t Expected: %u\n", buf[0], send_it[0] + send_it[1]);
    if(buf[0] == (send_it[0] + send_it[1])){
     success++;
    }
   }
  }else{
   break;
  }
 }

 kill(process_id, SIGTERM);
 waitpid(process_id, NULL, 0);
 entry->success_rate = ((float)success)/tests;
}

void propagate(char* name, char* path){
 
 pid_t process_id = fork();
 if(process_id == 0){
  //"Jailing" the child. Hey, it's better than nothing,
  chdir(path);
  chroot(path);
  
  //Formatting name into executable.
  size_t length = strlen(name);
  char* executable = malloc(length + 3);
  executable[0] = '.';
  executable[1] = '/';
  memcpy(&executable[2], name, length + 1);

  char arg1[] = "Makey baby!";
  char* argv[3];
  argv[0] = executable;
  argv[1] = arg1;
  argv[2] = NULL;   

  execve(executable,argv,NULL);
  exit(EXIT_FAILURE);   
 }

 //This is gonna fail someday
 waitpid(process_id, NULL, 0);
 
}

void initialize(){
 my_pid = getpid();

 pipe(out);
 pipe(in);
}

float timeTaken(struct timespec* s, struct timespec* e){
 float elapsed = 0;
 long sec, nano;
 sec = e->tv_sec - s->tv_sec;
 nano = e->tv_nsec - s->tv_nsec;
 
 if(nano < 0){
  nano += 1000000000;
  sec--;
 }
 
 elapsed = ((float)nano) / 1000000000 + sec;
 return elapsed;
}

int run(char* directory, int loops){
 initialize();
 
 struct timespec start, end;

 //Loop;
 int loop = 0;
 while(loop < loops){
  loop++;
  printf("\n%d/%d: ", loop, loops);
  fflush(stdout);

  char** specimen_sources;
  size_t specimen_sources_count;


  clock_gettime(CLOCK_MONOTONIC, &start);
  getSpecimens(directory, &specimen_sources, &specimen_sources_count);
  clock_gettime(CLOCK_MONOTONIC, &end);
 
  printf("Population: %zu\n", specimen_sources_count);
 
  printf("Getting specimens: %.3f\n", timeTaken(&start, &end));
  fflush(stdout);

  size_t specimen_count = desired_sample_size;
  
  //If we don't have enough specimen_sources to fully populate specimen_count
  if(specimen_count > specimen_sources_count){
   //Then reduce specimen_count.
   specimen_count = specimen_sources_count;
  } 

  //Allocate space for population
  Evo_Entry* population = selectSpecimens(specimen_sources, specimen_sources_count, specimen_count);

  printf("Mutating: ");
  fflush(stdout);
  clock_gettime(CLOCK_MONOTONIC, &start);

  //Mutate the specimens
  if(!(specimen_count < desired_sample_size)){
   for(size_t i = 0; i < specimen_count; i++){
    mutateFile(population[i].name, directory);
   }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("%.3f\n", timeTaken(&start, &end));

  printf("Testing: ");
  fflush(stdout);
  clock_gettime(CLOCK_MONOTONIC, &start);

  //Test and grade specimens.
  for(size_t i = 0; i < specimen_count; i++){
   test(&population[i], directory);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("%.3f\n", timeTaken(&start, &end));

  clock_gettime(CLOCK_MONOTONIC, &start); 

  //Calculate weights
  float* reproduction_table = malloc(specimen_count * sizeof (*reproduction_table));
  float* kill_table = malloc(specimen_count * sizeof (*kill_table));
  
  //Calculate sum of success_rates.
  float success_sum = 0;
  for(size_t i = 0; i < specimen_count; i++){
   success_sum += population[i].success_rate;
  }

  float kill_sum = specimen_count - success_sum;
  float mercy_fragment; 
  float unlucky_fragment;
  float mercy_sum; 
  float unlucky_sum;  

  if(success_sum == 0){
   success_sum = 1;
   mercy_fragment = success_sum * mercy / specimen_count;
   mercy_sum = 1;
  }else{
   mercy_fragment = success_sum * mercy / specimen_count;
   mercy_sum = success_sum * (1 + mercy);
  }
  
  if(kill_sum == 0){
   kill_sum = 1;
   unlucky_fragment = kill_sum * unlucky / specimen_count;
   unlucky_sum = 1;
  }else{
   unlucky_fragment = kill_sum * unlucky / specimen_count;
   unlucky_sum = kill_sum * (1 + unlucky);
  }

  for(size_t i = 0; i < specimen_count - 1; i++){
   reproduction_table[i + 1] = reproduction_table[i] + (population[i].success_rate + mercy_fragment) / mercy_sum;
   kill_table[i + 1] = kill_table[i] + (1 - population[i].success_rate + unlucky_fragment) / unlucky_sum;
  }

 
  if(loop == loops){
   printf("Stats:\nNum)\tName\t\tRate\tRep\tKill\n");
   for(size_t i = 0; i < specimen_count; i++){
    printf("%zu\t%s\t%.2f%%\t%.2f\t%.2f\n",i,population[i].name, population[i].success_rate*100, reproduction_table[i]*100, kill_table[i]*100);
   }
  }
   
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Calculating weight tables: ");
  fflush(stdout);
  printf("%.3f\n", timeTaken(&start, &end));

  printf("Reproducing: ");
  fflush(stdout);
  clock_gettime(CLOCK_MONOTONIC, &start);

  //Reproduce specimens.
  size_t spawns = specimen_count * 0.5f;
  if(spawns < 1){
   spawns = 1;
  }
  for(size_t i = 0; i < spawns; i++){
   //Pick a random float [0,1]
   float gods_choice = ((float)rand())/RAND_MAX;

   size_t gods_chosen = fit(reproduction_table, sizeof (*reproduction_table), &floatFloatCompare, gods_choice, 0, specimen_count - 1);
   
   //printf("God's Choice: %f\tGod's Chosen: %zu\n", gods_choice, gods_chosen);

   //Find the index in reproduction_table closest to gods_choice without going over.
   //Reproduce the specimen at that index in population.
   propagate(population[gods_chosen].name, directory);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("%.3f\n", timeTaken(&start, &end));

  printf("Killing: ");
  fflush(stdout);
  clock_gettime(CLOCK_MONOTONIC, &start);

  //Kill specimens.
  char* death_note = calloc(specimen_count,1);

  ssize_t attempts = ((ssize_t)specimen_sources_count) - ((ssize_t)desired_population);
  //printf("Kill attempts: %zd\n", attempts);
  for(ssize_t i = 0; i < attempts; i++){
   //printf("Marking for death\n");
   //Same as reproduction, but with kill_table and we kill the lucky winner.
   float gods_choice = ((float)rand())/RAND_MAX;

   size_t gods_chosen = fit(kill_table, sizeof (*kill_table), &floatFloatCompare, gods_choice, 0, specimen_count - 1);

   death_note[gods_chosen] = 1;
  }
 
  //Delete specimens marked for death.
  for(size_t i = 0; i < specimen_count; i++){
   //For clarity and neatness
   //If this specimen is not marked for death
   if(death_note[i] == 0){
    //Then skip it.
    continue;
   }

   //Format file_path
   char* name = population[i].name;
   size_t full_path_length = strlen(directory) + strlen(name) + 2;
   char* full_path = malloc(full_path_length);
   snprintf(full_path, full_path_length, "%s/%s", directory, name);
   unlink(full_path);
   free(full_path);   
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("%.3f\n", timeTaken(&start, &end));

  printf("Freeing: ");
  fflush(stdout);
  clock_gettime(CLOCK_MONOTONIC, &start);

  //printf("death_note/n");
  free(death_note);

  //printf("population/n");
  free(population);
  //printf("reproduction_table/n");
  free(reproduction_table);
  //printf("kill_table/n");
  free(kill_table);

  //printf("specimen_sources[i]/n");
  for(size_t i = 0; i < specimen_sources_count; i++){
   free(specimen_sources[i]);
  }
  //printf("specimen_sources/n");
  free(specimen_sources);

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("%.3f\n", timeTaken(&start, &end));
  
 }//End loop

 close(out[0]);
 close(out[1]);
 close(in[0]);
 close(in[1]);

 return 0;
}
