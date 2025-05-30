#include "types.h"

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

int
memcmp(const void *v1, const void *v2, uint n)
{
  const uchar *s1, *s2;

  s1 = v1;
  s2 = v2;
  while(n-- > 0){
    if(*s1 != *s2)
      return *s1 - *s2;
    s1++, s2++;
  }

  return 0;
}

void*
memmove(void *dst, const void *src, uint n)
{
  const char *s;
  char *d;

  if(n == 0)
    return dst;
  
  s = src;
  d = dst;
  if(s < d && s + n > d){
    s += n;
    d += n;
    while(n-- > 0)
      *--d = *--s;
  } else
    while(n-- > 0)
      *d++ = *s++;

  return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char*
strncpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  while(n-- > 0)
    *s++ = 0;
  return os;
}

// Like strncpy but guaranteed to NUL-terminate.
char*
safestrcpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  if(n <= 0)
    return os;
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0;
  return os;
}

int
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

char *
strcat(char *dest, const char *src)
{
    uint i,j;
    for (i = 0; dest[i] != '\0'; i++)
        ;
    for (j = 0; src[j] != '\0'; j++)
        dest[i+j] = src[j];
    dest[i+j] = '\0';
    return dest;
}

/* Prepends t into s. Assumes s has enough space allocated
** for the combined string.
*/
void strprep(char* s, const char* t)
{
    uint len = strlen(t);
    memmove(s + len, s, strlen(s) + 1);
    memcpy(s, t, len);
}


#define LONG_MAX (unsigned long)9223372036854775807
#define LONG_MIN (long) -9223372036854775807

int isspace(char c) {
 return c ==' ';
}

int isdigit(char c)
{
  char digits[] = "1234567890";
  for (int i=0; i<11;i++ )
  {
    if (c == digits[i])
    {
      return 1;
    }
  }
  return 0;
}

int isalpha(char c)
{
  char letters[] = "abcdefghijklmnopqrstuvwxyz";
  for (int i=0; i<11;i++ )
  {
    if (c == letters[i])
    {
      return 1;
    }
  }
  return 0;
}

int isupper(char c)
{
  char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (int i=0; i<11;i++ )
  {
    if (c == letters[i])
    {
      return 1;
    }
  }
  return 0;
}

#define ISSPACE(c) isspace(c)
#define ISALPHA(c) isalpha(c)
#define ISDIGIT(c) isdigit(c)
#define ISUPPER(c) isupper(c)

long
strtol(const char *nptr, char **endptr, register int base)
{
	register const char *s = nptr;
	register unsigned long acc;
	register int c;
	register unsigned long cutoff;
	register int neg = 0, any, cutlim;

	/*
	 * Skip white space and pick up leading +/- sign if any.
	 * If base is 0, allow 0x for hex and 0 for octal, else
	 * assume decimal; if base is already 16, allow 0x.
	 */
	do {
		c = *s++;
	} while (ISSPACE(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
	 * Compute the cutoff value between legal numbers and illegal
	 * numbers.  That is the largest legal value, divided by the
	 * base.  An input number that is greater than this value, if
	 * followed by a legal input character, is too big.  One that
	 * is equal to this value may be valid or not; the limit
	 * between valid and invalid numbers is then based on the last
	 * digit.  For instance, if the range for longs is
	 * [-2147483648..2147483647] and the input base is 10,
	 * cutoff will be set to 214748364 and cutlim to either
	 * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
	 * a value > 214748364, or equal but the next digit is > 7 (or 8),
	 * the number is too big, and we will return a range error.
	 *
	 * Set any if any `digits' consumed; make it negative to indicate
	 * overflow.
	 */
	cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
	cutlim = cutoff % (unsigned long)base;
	cutoff /= (unsigned long)base;
	for (acc = 0, any = 0;; c = *s++) {
		if (ISDIGIT(c))
			c -= '0';
		else if (ISALPHA(c))
			c -= ISUPPER(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = neg ? LONG_MIN : LONG_MAX;
		
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *) (any ? s - 1 : nptr);
	return (acc);
}


#include <stdarg.h>

static char digits[] = "0123456789abcdef";
static void
sprintint(long long xx, int base, int sign, char * buf)
{
  char innerbuf[16];
  int i;
  unsigned long long x;

  if(sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    innerbuf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    innerbuf[i++] = '-';

  int j = 0;
  while(--i >= 0)
    buf[j++] = innerbuf[i];
}

int
sprintf(char * buf, char *fmt,...)
{
  va_list ap;
  int i, cx, c0, c1, c2;
  // char *s;

  va_start(ap, fmt);
  for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){
      buf[i] = cx;
      continue;
    }
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if(c0) c1 = fmt[i+1] & 0xff;
    if(c1) c2 = fmt[i+2] & 0xff;
    if(c0 == 'd'){
      sprintint(va_arg(ap, int), 10, 1, buf);
    } else if(c0 == 'l' && c1 == 'd'){
      sprintint(va_arg(ap, uint64), 10, 1, buf);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
      sprintint(va_arg(ap, uint64), 10, 1, buf);
      i += 2;
    } else if(c0 == 'u'){
      sprintint(va_arg(ap, int), 10, 0, buf);
    } else if(c0 == 'l' && c1 == 'u'){
      sprintint(va_arg(ap, uint64), 10, 0, buf);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
      sprintint(va_arg(ap, uint64), 10, 0, buf);
      i += 2;
    } else if(c0 == 'x'){
      sprintint(va_arg(ap, int), 16, 0, buf);
    } else if(c0 == 'l' && c1 == 'x'){
      sprintint(va_arg(ap, uint64), 16, 0, buf);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
      sprintint(va_arg(ap, uint64), 16, 0, buf);
      i += 2;
    
    // } else if(c0 == 'p'){
    //   printptr(va_arg(ap, uint64));
    // } else if(c0 == 's'){
    //   if((s = va_arg(ap, char*)) == 0)
    //     s = "(null)";
    //   for(; *s; s++)
    //     consputc(*s);
    // } else if(c0 == '%'){
    //   consputc('%');
    } else if(c0 == 0){
      break;
    } else {
      // Print unknown % sequence to draw attention.
      // consputc('%');
      // consputc(c0);
    }

  }
  va_end(ap);

  return 0;
}