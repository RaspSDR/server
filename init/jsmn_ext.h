#pragma once

#define JSMN_HEADER
#include "jsmn.h"

#define JSMN_IS_ID(jt)		    ((jt) != NULL && (jt)->type == JSMN_STRING && (jt)->size == 1)
#define JSMN_IS_STRING(jt)	    ((jt) != NULL && (jt)->type == JSMN_STRING && (jt)->size == 0)
#define JSMN_IS_OBJECT(jt)	    ((jt) != NULL && (jt)->type == JSMN_OBJECT)
#define JSMN_IS_ARRAY(jt)	    ((jt) != NULL && (jt)->type == JSMN_ARRAY)
#define JSMN_IS_PRIMITIVE(jt)	((jt) != NULL && (jt)->type == JSMN_PRIMITIVE)