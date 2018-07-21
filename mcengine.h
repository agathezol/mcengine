#ifndef _hyde_mcengine_h
#define _hyde_mcengine_h

#define  MCENGINE_VERSION "1.04"
#ifndef buildtime
#   define buildtime "unknown"
#endif

typedef enum {
	SEC_NORM = 0,
	SEC_PRIV,
	SEC_ADMIN,
	SEC_MAX
} SECURITY_LEVEL;

typedef struct {
	char username[32];
	SECURITY_LEVEL sl;
} SEC_USER;

typedef struct {
	int count;
	SEC_USER *user;
} SEC_USER_LIST;

#endif
