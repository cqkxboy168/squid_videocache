#ifndef _ACSM_H
#define _ACSM_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define LEN 1024
#define ACSM_NUM 		2
#define ALPHABET_SIZE	256	                //size of  alphabet
		
#define ACSM_FAIL_STATE 	-1	           		//fail state
#define SOLID_STATE    		0	               //solid state
#define VIRTUAL_STATE1		1	               //virtual state1
#define VIRTUAL_STATE2		2	               //virtual state2
#define VIRTUAL_STATE3		3	               //virtual state3



/*
**struct of pattern
*/
typedef struct _acsm_pattern {
	struct _acsm_pattern    *next;				//pouint32_ter to next pattern
	unsigned char			*patrn_cap; 		//pointer of pattern's captital
	unsigned int				n_cap;					//length of pattern
  unsigned int 	 			nmatch;				//number of successes of matching pattern
  unsigned int urlflag; // identification number of different video web url

} ACSM_PATTERN;

/*
**struct of state
*/
typedef struct {	
    unsigned		flag;							//flag of state:solid or virtual?			
	unsigned 	NextState[ALPHABET_SIZE];		//next state-based on input character
	unsigned 	FailState;						//fail state-used when building NFA & DFA
	ACSM_PATTERN *MatchList;					//list of patterns those and here,if any
} ACSM_STATETABLE;

/*
**struct of state machine
*/
typedef struct {
	unsigned acsmMaxState;			       		//maximum number of states
	unsigned acsmNumState;						//number of states
	ACSM_PATTERN	*acsmPatterns;		       //pointer to patterns
	ACSM_STATETABLE	*acsmStateTable;	       //pointer to state table
} ACSM_STRUCT;

extern ACSM_STRUCT *acsm_cap[ACSM_NUM];

int init_acsm(int n,const char *fileName);
void ConvertCaseEX(unsigned char *d,unsigned char *s,uint32_t m);
int acsmSearch_cap(ACSM_STRUCT *acsm,unsigned char *Tx,uint32_t n);

/*
**print the sunmmary of matching
*/
void PrintSummary (ACSM_STRUCT *acsm);
#endif

