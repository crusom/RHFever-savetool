#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>

#include "crc32.h"

#define UNLOCKED_LEVEL_GROUPS_OFFSET 0x09
#define LEVEL_POINTS_OFFSET 0x3e
#define LEVEL_STATUSES_OFFSET 0x0b
#define LAST_PLAYED_LEVEL_OFFSET 0x0a
#define STATUS_PREV_PLAYED_LEVEL 0xa3
#define POINTS_MULT 0xa4

#define INT32_SWAP(num) ((num>>24)&0xff) | ((num<<8)&0xff0000) | ((num>>8)&0xff00) | ((num<<24)&0xff000000);
#define INT16_SWAP(num) ((num>>8)&0xff) | ((num<<8)&0xff00);

#define ERR_GO(label, fmt, ...) do {fprintf(stderr, fmt, __VA_ARGS__); goto label;} while(0);

const char *level_names[] = {
  "Hole in One", "Screwbot Factory", "See-Saw", "Double Date", "1st Remix",
  "Fork Lifter", "Tambourine", "Board Meeting", "Monkey Watch", "2nd Remix",
  "Working Dough", "Built to Scale", "Air Rally", "Figure Fighter", "3rd Remix",
  "Ringside", "Packing Pests", "Micro-Row", "Samurai Slice", "4th Remix",
  "Catch of the Day", "Flipper-Flop", "Exhibition Match", "Flock Step", "5th Remix",
  "Launch Party", "Donk-Donk", "Bossa Nova", "Love Rap", "6th Remix",
  "Tap Troupe", "Shrimp Shuffle", "Cheer Readers", "Karate Man", "7th Remix",
  "Samurai Slice 2", "Working Dough 2", "Built To Scale 2", "Double Date 2", "8th Remix",
  "Love Rap 2", "Cheer Readers 2", "Hole in One 2", "Screwbot Factory 2", "9th Remix",
  "Figure Fighter 2", "Micro-Row 2", "Packing Pests 2", "Karate Man 2", "10th Remix"
};

const char *status_string[] = {"Unavailable", "Unlocked in column", "Play", "Ok", "Superb", "Perfect"};

static char *arg0 = NULL;

static void print_help(const char *arg0) {
  printf("Usage: %s [options...] <file>\n\
          no arguments lists all levels statuses and flow\n\
          -u,  --unlock <level> to unlock up to <level>\n\
          -c,  --change <level> <points in hex> <status> change level points and status\n\
          -h,  --help         Print this\n\
          level: 1-50\n\
          points: 0x0000-0xfffe\n\
          status: ok, superb, perfect\
          \n", arg0);
}
          //-f,  --flow <num>   change flow to\n\

static int map_save(char *fname, void **data) {
    int fd;
    if((fd = open(fname, O_APPEND | O_RDWR, 0)) < 0) {
      perror("open");
      exit(EXIT_FAILURE);
    }
    struct stat st;
    fstat(fd, &st);
    if(st.st_size != 2592) {
      fprintf(stderr, "invalid file size, is %d and should be 2592", st.st_size);
      exit(EXIT_FAILURE);
    }

    if((*data = mmap(0, 2592, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
      perror("mmap");
      exit(EXIT_FAILURE);
    }
    return fd;
}


static void save_checksum(void *save) {*(uint32_t*)(save + 0xa00) = INT32_SWAP(crc32_byte(save, 0xa00));}

static void game_has_started(void *save) {*(uint8_t*)(save + 0x3d) = 0x3;}

static void group_adjust(void *save, int max_level) {
  if (max_level != 50) {
    ((uint8_t*)(save + LEVEL_STATUSES_OFFSET))[max_level] = 0x2;

    for(int i = max_level + 1; i <= max_level + (4 - max_level % 5); i++)
      ((uint8_t*)(save + LEVEL_STATUSES_OFFSET))[i] = 0x1;
   }

  *(uint8_t*)(save + UNLOCKED_LEVEL_GROUPS_OFFSET) = (max_level == 50) ? 0x09 : max_level / 5;
  *(uint8_t*)(save + LAST_PLAYED_LEVEL_OFFSET) = 0x05 + max_level - 1; 
  *(uint8_t*)(save + STATUS_PREV_PLAYED_LEVEL) = ((uint8_t*)(save + LAST_PLAYED_LEVEL_OFFSET))[max_level]; 
  *(uint16_t*)(save + POINTS_MULT) = INT16_SWAP(0x2137);
}

static void unlock(void *save, int unlock_from, int unlock_up_to) {
  int unlocked_levels_num = 0;  
  for (int i = 0; i < 50; i++) 
    if (((uint8_t*)(save + LEVEL_STATUSES_OFFSET))[i] >= 2) unlocked_levels_num++;
    else break;
  
  if (unlocked_levels_num > unlock_up_to)
    for (int i = unlock_up_to; i < 50; i++) {
      ((uint8_t*)(save + LEVEL_STATUSES_OFFSET))[i] = 0x0;
      ((uint16_t*)(save + LEVEL_POINTS_OFFSET))[i]  = INT16_SWAP(0xFFFF);
    } 

  for(int i = unlock_from - 1; i < unlock_up_to; i++)
    ((uint8_t*)(save + LEVEL_STATUSES_OFFSET))[i] = 0x3;
    
  for(int i = unlock_from - 1; i < unlock_up_to; i++)
    ((uint16_t*)(save + LEVEL_POINTS_OFFSET))[i] = INT16_SWAP(0x2137);
      
  group_adjust(save, unlock_up_to); 
  game_has_started(save);  
  save_checksum(save);
}

static void change(void *save, int level, int points, int status) {
  int unlocked_levels_num = 0;  
  for (int i = 0; i < 50; i++) 
    if (((uint8_t*)(save + LEVEL_STATUSES_OFFSET))[i] >= 2) unlocked_levels_num++;
    else break;

  unlocked_levels_num = (unlocked_levels_num == 0) ? 1 : unlocked_levels_num;
  
  if (level >= unlocked_levels_num)
      unlock(save, unlocked_levels_num, level);

  ((uint8_t*)(save + LEVEL_STATUSES_OFFSET))[level - 1] = status;
  ((uint16_t*)(save + LEVEL_POINTS_OFFSET))[level - 1]  = INT16_SWAP(points);
   
  game_has_started(save);  
  save_checksum(save);
}     

static int get_flow(void *save) {

  int levels_len = 0;
  int points_sum = 0;
  int flow = 0;
  
  for(int i = 0; i < 50; i++) {
    uint16_t points = INT16_SWAP(((uint16_t*)(save + LEVEL_POINTS_OFFSET))[i]);
    if (points != 0xffff) {
      levels_len += 1;
      points_sum += points + 0x32;
    }
  }
  
  if (levels_len == 0 || points_sum == 0) return 0;
  
  flow = (levels_len - 1) * 0x46;
  flow = flow / 0x32 + (flow >> 0x1f);
  points_sum = (points_sum / levels_len) / 100 + (points_sum / levels_len >> 0x1f);
  points_sum = points_sum - (points_sum >> 0x1f);
  if (points_sum < 0)
    levels_len = 0;
  else {
    levels_len = 100;
    if (points_sum < 0x65)
      levels_len = points_sum;
  }
  levels_len = levels_len * ((flow - (flow >> 0x1f)) + 0x32);
  points_sum = levels_len / 100 + (levels_len >> 0x1f);
  flow = (points_sum - (points_sum >> 0x1f)) + 0x14;

//  if (flow < 0 || flow > 150)
//    fprintf(stderr, "%s", "INFO: Invalid flow!");  
  assert(flow >= 0 && flow <= 150);
  
  return flow;
}

int main(int argc, char **argv) {
  int fd;
  int c;
  void *save;
  int unlock_up_to = -1;
  
  if (argc == 1) {
    print_help(argv[0]);
    exit(EXIT_SUCCESS);
  }
  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      print_help(argv[0]);
      return EXIT_SUCCESS;
  }
  
  fd = map_save(argv[argc - 1], &save);
  
  if (argc == 2) {
    for (int i = 0; i < 50; i++) {
      uint16_t points = INT16_SWAP(((uint16_t*)(save + LEVEL_POINTS_OFFSET))[i]);
      uint8_t status = *(uint8_t*)(save + LEVEL_STATUSES_OFFSET + i);
      if (status < 0 || status > 5)
        ERR_GO(err, "wrong status byte \"%d\" at level %d, corrupted save", status, i + 1);
      printf("%d %s: %x (%s)\n", i+1, level_names[i], points, status_string[status]);
    }
    printf("flow: %d\n", get_flow(save));
    goto end;
  }

  char *arg = argv[1];
  if (strcmp(arg, "-u") == 0 || strcmp(arg, "--unlock") == 0) {
      if (argv[2] == NULL || isspace(argv[2][0])) 
        ERR_GO(err, "%s", "wrong -u option argument\n");
      
      unlock_up_to = strtol(argv[2], NULL, 10);
      
      if (unlock_up_to <= 0 || unlock_up_to > 50)
        ERR_GO(err, "%s", "wrong -u option argument\n");
      
      unlock(save, 1, unlock_up_to);
  } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--change") == 0) {
    if (argc != 6 || !argv[2] || !argv[3] || !argv[4])  
      ERR_GO(err, "%s", "3 arguments for -c needed\n");

    int level, points, status;    
    if((level = strtol(argv[2], NULL, 10)) < 0 || level > 50)
      ERR_GO(err, "level should be in 0-50, but it's %d\n", level);
    if((points = strtol(argv[3], NULL, 16)) >= 0xffff)
      ERR_GO(err, "points should be less than 0xffff, but it's %d\n", points);
     
    char *status_str = argv[4];
    if(strcmp(status_str, "ok") == 0)
      status = 3;
    else if(strcmp(status_str, "superb") == 0)
      status = 4;    
    else if(strcmp(status_str, "perfect") == 0)
      status = 5;
    else
      ERR_GO(err, "status should be \"ok\" or \"superb\" or \"perfect\", but is \"%s\"\n", status_str);

    change(save, level, points, status);     
  } else
      ERR_GO(err, "Invalid argument: %s\n", arg);
  
  puts("done!");
  
  end:
  close(fd);
  return EXIT_SUCCESS;

  err:
  close(fd);
  return EXIT_FAILURE;
}
