#!/bin/bash 

GITCMD=$(which git)

FULLTIME=`date`
YEAR=`date +%Y`

cat > version.hpp << EOF
#define BUILD_DATE "$FULLTIME"
#define LOGNOT_VERSION "v0.2"
EOF

# Get git commit hash and append to the version only if
# there is git installed and it is git repository
GITREV=""
if [ -n "$GITCMD" -a -e "./.git" ]; then
	GITREV=`$GITCMD rev-parse HEAD | head -c 8`
	cat >> version.hpp << EOF
#define COMMIT "-$GITREV"
EOF
fi