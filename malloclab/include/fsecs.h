#ifndef FSECS_H_
#define FSECS_H_

typedef void (*fsecs_test_funct)(void *);

void init_fsecs(void);
double fsecs(fsecs_test_funct f, void *argp);

#endif /* FSECS_H_ */