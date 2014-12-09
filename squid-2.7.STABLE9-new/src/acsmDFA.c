#include <assert.h>
#include "acsmDFA.h"

#define MAXPATTERNLEN 257
#define SHOW_PATTERN_RESULT



ACSM_STRUCT *acsm_cap[ACSM_NUM] = {NULL};


static unsigned int nfound = 0;
/************************************************ 
*匹配是否发现字符串函数
* 输入： 
* pattern:自动机模式串集合指针
* mlist: 发现的模式串
* 返回值： 发现字符串的flag
************************************************/ 
int PrintMatch (ACSM_PATTERN * pattern,ACSM_PATTERN * mlist) 
{
	/* Count the Each Match Pattern */
   ACSM_PATTERN *temp = pattern;
	for (;temp!=NULL;temp=temp->next) {
		if(!strcmp((const char *)temp->patrn_cap,(const char *)mlist->patrn_cap)){
			temp->nmatch++;
			nfound++;
			return temp->urlflag;
			break;
		}		
	}
	return 0;
}

/************************************************ 
*输出审计结果并清0函数
* 输入： 
* pattern:自动机模式串集合指针
* 返回值： 
************************************************/ 
void PrintSummary (ACSM_STRUCT *acsm)
{
	ACSM_PATTERN * mlist = acsm->acsmPatterns;

	for (;mlist!=NULL;mlist=mlist->next){
	#ifdef SHOW_PATTERN_RESULT
		printf("***%-20s:%-5d\n",mlist->patrn_cap,mlist->nmatch);
	#endif
		mlist->nmatch = 0;			
	}
#ifdef SHOW_PATTERN_RESULT
	printf("####Total matching number is:%d#####\n\n\n",nfound);
#endif
	nfound=0;
}

/*
*Malloc the AC memory
*/
static void *AC_MALLOC(uint32_t n)
{
	void *p;
	p = malloc(n);
	return p;
}

/*
*Free the AC memory
*/
static void AC_FREE(void *p)
{
	if(p)
       free(p);
	p = NULL;
}

/*
*Struct of Queue Node
*/
typedef struct _qnode {
	uint32_t state;			
	struct _qnode *next;		
}QNODE;

/*
*Struct of Queue
*/
typedef struct _queue {
	QNODE *head,*tail;			
	uint32_t count;				
}QUEUE;

/*
*Init the Queue
*/
static void queue_init(QUEUE *s)
{
	s->head = s->tail = NULL;			
	s->count = 0;
}
/*
*Add node to the queue from tail
*/
static void queue_add(QUEUE *s, uint32_t state)
{
	QNODE *q;
	if(!s->head) {
		q=s->tail=s->head=(QNODE *)AC_MALLOC(sizeof(QNODE)); 
		//MEMASSERT(q,"queue_add");	
		q->state=state;			
		q->next=NULL;
	}
	else {
		q=(QNODE *)AC_MALLOC(sizeof(QNODE));
		//MEMASSERT(q,"queue_add");
		q->state=state;
		q->next=NULL;
		s->tail->next=q;
		s->tail=q;		       
	}

	s->count++;
}

/*
*Remove node from haed of the queue
*/
static int queue_remove(QUEUE *s)
{
	uint32_t state=0;
	QNODE *q;
	if(s->head) {
		q=s->head;				
		state=q->state;
		s->head=s->head->next;   	
		s->count--;
		if(!s->head) {
			s->tail=NULL;
			s->count=0;
		}

		AC_FREE(q);
	}

	return state;
}

/*
*Get the count of the node in the queue
*/
static int queue_count(QUEUE *s)
{
	return s->count;
}

/*
*Free the queue memory
*/
static void queue_free(QUEUE *s)
{
	while(queue_count(s)) {
		queue_remove(s);
	}
}
/*
*case translation table
*/
static unsigned char xlatcase[256];
/*
*init the xlatecase table,trans alpha to UpperMode, just for the N0Case state.For example:b->B,D->D
*/

static void init_xlatcase()
{
	uint32_t i;
	for(i=0;i<256;i++) {
		xlatcase[i] = (unsigned char)toupper(i);
	}
}

/*
*Convert the pattern string into uppermode,for example:beijing->BEIJING
*/
void ConvertCaseEX(unsigned char *d,unsigned char *s,uint32_t m)
{
	uint32_t i;
	for(i=0; i<m; i++) {
		d[i]=xlatcase[s[i]];
	}
}



/*
*   Add a pattern to the list of patterns for this state machine
*/ 
static int acsmAddPattern(ACSM_STRUCT *p, unsigned char *pat, uint32_t n,uint32_t urlflag) 
{
	ACSM_PATTERN *plist;
	int len = 0;

	plist = (ACSM_PATTERN *)AC_MALLOC(sizeof(ACSM_PATTERN));
	//MEMASSERT(plist, "acsmAddPattern");
	
	plist->patrn_cap = (unsigned char *)AC_MALLOC(n+1);
	//MEMASSERT(plist->patrn_cap, "acsmAddPattern");
	memset(plist->patrn_cap + n, 0,1);
	ConvertCaseEX(plist->patrn_cap, pat, n);
	plist->n_cap = n;
	
	plist->nmatch=0;

  plist->urlflag=urlflag;
  
	/*Add the pattern into the pattern list*/
	plist->next = p->acsmPatterns;
	p->acsmPatterns = plist;

	return 0;
}


int acsm_parse_line(const char *urlFile, ACSM_STRUCT *acsm)
{
	char line[LEN];
	unsigned char keychar[LEN];
	unsigned char siteflag[3];
  unsigned char *start,*p,*q,*k;//p is the end point of keychar,k is the start point of siteflag,q is the end point of siteflag
	FILE *kwFp = fopen(urlFile, "r");

	if (!kwFp) {
		fprintf(stderr, "Open url file error!\n");
		return -1;
	}

	memset(line, 0, sizeof(line));
  
	while (fgets(line, LEN, kwFp))
	{
		assert(line != NULL);
		if (strlen(line) >= MAXPATTERNLEN)
			continue;
		if (line[0] == '\0' || line[0] == '#')
			continue;
    start= line;
		p = line;

		while(*p != '\0' || *p !='\n' || *p== '\r')
		{
		 if (*p==' ' || *p== '\t' ) {
				q = p+1;
				break;
			}
			else {
				p++;
			}
		}
		
		memset(keychar, 0, sizeof(keychar));
		
	  strncpy(keychar,line,p-start);
	  
	  while(*q != '\0')
	  {
	  	if(*q-'0'>=0 && *q-'0'<=9){
	  	   k=q;
	  	   break;
	  	}
	  	else
	  		q++;
	  }
	  
	  while(*q != '\0')
	  {
	  	if (*q==' ' || *q== '\t' || *q=='\n' || *q== '\r' ) {
				break;
			}
		  else if(*q-'0'>=0 && *q-'0'<=9){
		  	q++;
			}
			else {
				printf("grammar error,.txt flag must number!\n");
				return 0;
			}
	  }
	  memset(siteflag, 0, sizeof(siteflag));
	  if(q-k<= 3)
	    strncpy(siteflag,k,q-k);
	  else
	  	printf("grammar error,.txt flag too long!\n");
	   
	  printf("key:%s  flag:%d\n",keychar,atoi(siteflag));
		if (line[0] != '\0') {
			acsmAddPattern((ACSM_STRUCT *)acsm, (unsigned char *)keychar, strlen(keychar),atoi(siteflag));
		}
	}

	fclose(kwFp);

	return 0;
}

/*
*add a pattern to the list of patterns terminated at this state, insert at front of it
*/
static void AddMatchListEntry(ACSM_STRUCT *acsm, uint32_t state, ACSM_PATTERN *px)
{
	ACSM_PATTERN *p,*q;
	p=(ACSM_PATTERN *)AC_MALLOC(sizeof(ACSM_PATTERN));
	//MEMASSERT(p,"AddMatchListEntry");
	memcpy(p,px,sizeof(ACSM_PATTERN));
	q=acsm->acsmStateTable[state].MatchList;
	while (q!=NULL) {
		if(!strcmp((const char *)p->patrn_cap,(const char *)q->patrn_cap))
			return;
		q=q->next;
	}

	p->next=acsm->acsmStateTable[state].MatchList;
	acsm->acsmStateTable[state].MatchList=p;
}

/*
*Add pattern states
*/
static void AddPatternStates(ACSM_STRUCT *acsm,ACSM_PATTERN *p)
{
	unsigned char *pattern;
	uint32_t state=0,n,next,temp;
	n=p->n_cap;
	pattern=p->patrn_cap;
	acsm->acsmStateTable[0].flag=SOLID_STATE;

	//Match up pattern with existing states
	for (;n>0;pattern++,n--) {
		next=acsm->acsmStateTable[state].NextState[*pattern];
		if (next == ACSM_FAIL_STATE)				     
			break;
		state=next;	
	}	

	//Add new states for the rest of the pattern bytes,1 state per byte
	for (;n>0;pattern++,n--) {
		temp=state;
		acsm->acsmNumState++;
		acsm->acsmStateTable[state].NextState[*pattern]=acsm->acsmNumState;
		state=acsm->acsmNumState;
		acsm->acsmStateTable[state].flag=SOLID_STATE;
		if (*pattern >= 128 && acsm->acsmStateTable[temp].flag==SOLID_STATE) {
			acsm->acsmStateTable[state].flag=VIRTUAL_STATE1;	 
		}
	}

	/*Add the pattern to the MatchList of the state*/
	AddMatchListEntry(acsm,state,p);
}

/*
*Build NFA and DFA based on the NFA
*/
static void Build_DFA(ACSM_STRUCT *acsm)
{
	uint32_t r, s;
	uint32_t i;
	QUEUE q, *queue = &q;		
//	ACSM_PATTERN * mlist=0;		
//	ACSM_PATTERN * px=0;		

	/* Init a Queue */ 
	queue_init(queue);			

	/* Add the state 0 transitions 1st */
	/* 1st depth Node's FailState is 0, fail(x)=0 */
	for (i = 0; i < ALPHABET_SIZE; i++) {
		s = acsm->acsmStateTable[0].NextState[i];	
		if (s) {
			queue_add(queue, s);						
			acsm->acsmStateTable[s].FailState = 0;		
		}
	}

	/* Build the fail state transitions for each valid state */
	while (queue_count(queue) > 0) {
		r = queue_remove(queue);				   

		/* Find Final States for any Failure */ 
		for (i = 0; i < ALPHABET_SIZE; i++) {
			uint32_t fs, next;
			/**get the next state from r**/
			if ((s = acsm->acsmStateTable[r].NextState[i]) != ACSM_FAIL_STATE) {
				queue_add(queue, s);			
				fs = acsm->acsmStateTable[r].FailState; 
				/* 
				*  Locate the next valid state for 'i' starting at s 
				*/ 							
				while ((next=acsm->acsmStateTable[fs].NextState[i]) == ACSM_FAIL_STATE) {
					fs = acsm->acsmStateTable[fs].FailState;
				}
				/*
				 *  Update 's' state failure state to pouint32_t to the next valid state
				 */ 
				acsm->acsmStateTable[s].FailState = next;
			} else {
				acsm->acsmStateTable[r].NextState[i] = \
					acsm->acsmStateTable[acsm->acsmStateTable[r].FailState].NextState[i];
			}
		}
	}

	/* Clean up the queue */ 
	queue_free(queue);
}

/*
* Init the acsm DataStruct	
*/ 
ACSM_STRUCT *acsmNew() 
{
	ACSM_STRUCT *p;
	init_xlatcase();					
	p = (ACSM_STRUCT *) AC_MALLOC(sizeof(ACSM_STRUCT));	
	//MEMASSERT (p, "acsmNew");
	if (p)
		memset(p, 0, sizeof (ACSM_STRUCT));
	return p;
}

/*
*   Compile State Machine
*/ 
int acsmCompile_cap(ACSM_STRUCT * acsm) 
{
	uint32_t i, k;
	ACSM_PATTERN * plist;

	/* Count number of states */ 
	acsm->acsmMaxState = 1; /*State 0*/
	for (plist = acsm->acsmPatterns; plist != NULL; plist = plist->next) {
		acsm->acsmMaxState += plist->n_cap;
	}

	acsm->acsmStateTable = (ACSM_STATETABLE *)AC_MALLOC(sizeof(ACSM_STATETABLE) * \
			acsm->acsmMaxState);
	//MEMASSERT(acsm->acsmStateTable, "acsmCompile");
	memset(acsm->acsmStateTable, 0,sizeof (ACSM_STATETABLE) * acsm->acsmMaxState);
	/* Initialize state zero as a branch */ 
	acsm->acsmNumState = 0;

	/* Initialize all States NextStates to FAILED */
	for (k = 0; k < acsm->acsmMaxState; k++) {
		for (i = 0; i < ALPHABET_SIZE; i++) {
			acsm->acsmStateTable[k].NextState[i] = ACSM_FAIL_STATE;
		}
	}

	/* This is very important */
	/* Add each Pattern to the State Table */ 
	for (plist = acsm->acsmPatterns; plist != NULL; plist = plist->next) {
		AddPatternStates(acsm, plist);
	}

	/* Set all failed state transitions which from state 0 to return to the 0'th state */ 
	for (i = 0; i < ALPHABET_SIZE; i++) {
		if (acsm->acsmStateTable[0].NextState[i] == ACSM_FAIL_STATE) {
			acsm->acsmStateTable[0].NextState[i] = 0;
		}
	}

	/* Build the NFA  */ 
	Build_DFA(acsm);

	return 0;
}

/*64KB Memory*/
//static unsigned char Tc[MAXLINELEN];

/*
*   Search Text or Binary Data for Pattern matches
*/ 
int acsmSearch_cap(ACSM_STRUCT * acsm, unsigned char *Tx, unsigned int n) 
{
	unsigned state, temp, fail;
	ACSM_PATTERN *mlist;
	unsigned char *Tend;
	ACSM_STATETABLE * StateTable = acsm->acsmStateTable;
	unsigned char *T;

	nfound = 0;

	if((int) n <= 0){
		return -1;
	}

	/* Case conversion */
	/* ConvertCaseEX(Tc, Tx, n);
	T = Tc;
	Tend = Tc + n;*/
	//////////////////////////////////////////////////////////////////////////
	/*no case conversion*/
	T = Tx;
	Tend = Tx + n;
	//////////////////////////////////////////////////////////////////////////
	
	for (state = 0; T < Tend; T++) {
		temp=state;

		if(*T == '\0')
			return -1; 

		state = StateTable[state].NextState[*T];

		if(*T < 128) {
			 /* State is an accept state? */
			if( StateTable[state].MatchList != NULL ){
				for(mlist=StateTable[state].MatchList; mlist!=NULL;mlist=mlist->next){
					PrintMatch(acsm->acsmPatterns,mlist);
					return PrintMatch (acsm->acsmPatterns,mlist);
        }
      }

			fail=StateTable[state].FailState;

			while(fail!=0) {
				if(StateTable[fail].MatchList!=NULL && StateTable[fail].flag==SOLID_STATE){
					for(mlist=StateTable[fail].MatchList; mlist!=NULL;mlist=mlist->next){
						//nfound++;
						return PrintMatch (acsm->acsmPatterns,mlist);

					
					}
				}
				fail=StateTable[fail].FailState;
			}
		}
		else if(*T >= 128){
			/*forbide virtual state to virtual state*/
			if(StateTable[temp].flag==VIRTUAL_STATE1 && StateTable[state].flag==VIRTUAL_STATE1)
				state = 0;
		    /*jump one char to avoid wrong matching*/
			if(state==0 && StateTable[temp].flag == SOLID_STATE) 
				T++;
			else{
				if( StateTable[state].MatchList != NULL && StateTable[temp].flag==VIRTUAL_STATE1){
					for( mlist=StateTable[state].MatchList; mlist!=NULL;mlist=mlist->next){
						PrintMatch (acsm->acsmPatterns,mlist); 

						if (nfound) {
							return acsm->acsmPatterns->urlflag;
						}
					}
				}
				fail=StateTable[state].FailState;
				while(fail!=0){
					if(StateTable[fail].MatchList!=NULL && StateTable[fail].flag==SOLID_STATE){
						for(mlist=StateTable[fail].MatchList; mlist!=NULL;mlist=mlist->next){
							//nfound++;
							PrintMatch (acsm->acsmPatterns,mlist);

							return PrintMatch (acsm->acsmPatterns,mlist);
						}
					}
					fail=StateTable[fail].FailState;
				}
			}
			/* State is a accept state? */
		}
	}

	return 0;
	//return nfound;
}

/*
*   Free all memory
*/ 
void acsmFree (ACSM_STRUCT * acsm) 
{
	unsigned  i;
	ACSM_PATTERN * mlist, *ilist;

	if(NULL == acsm)
		return;

	for (i = 0; i < acsm->acsmNumState; i++) {
		if (acsm->acsmStateTable[i].MatchList != NULL) {
			mlist = acsm->acsmStateTable[i].MatchList;
			while (mlist) {
				ilist = mlist;	
				mlist = mlist->next;			
				AC_FREE (ilist->patrn_cap);
				AC_FREE (ilist);				
			}
		}
	}

	AC_FREE (acsm->acsmPatterns);
	AC_FREE (acsm->acsmStateTable);
	acsm = NULL;
}

/*
**获取模式串
*/
int acsmDFA(ACSM_STRUCT *acsm)
{
	// read patterns from conf_file
   
/*Generate GtoTo Table and Fail Table*/ 
	acsmCompile_cap(acsm);

	return 0;
}

int acsm_changed(uint8_t *msg, int len, uint8_t **retbuf, int *retlen)
{
	int i, ret;
	ACSM_STRUCT *acsm_old;
	for (i = 0; i < ACSM_NUM; i++) {
		ACSM_STRUCT *acsm_cap_new = NULL;
		acsm_cap_new = acsmNew();	
		ret = acsmDFA(acsm_cap_new);
		if(ret == -1){
			goto error;
		}

		if(acsm_cap[i]) {
			acsm_old = acsm_cap[i];
			acsm_cap[i] = acsm_cap_new;

			acsmFree(acsm_old);
		}
		else 
			acsm_cap[i] = acsm_cap_new;
	}

	return 0;

error:
	//fprintf(stderr, "malloc acsm failed: %s", strerror(errno));
	fprintf(stderr, "malloc acsm failed:\n");
	return -1;
}

int init_acsm(int n,const char *fileName)
{

	  int ret;
		acsm_cap[n] = acsmNew();
		
		acsm_parse_line(fileName, acsm_cap[n]);

		ret = acsmDFA(acsm_cap[n]);
		if(ret == -1){
			fprintf(stderr,"create acsm_gb fail!\n");
			return ret;
		}
	//get_interference();
	return 0;
}

