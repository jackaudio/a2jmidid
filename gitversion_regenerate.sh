#!/bin/sh

#set -x

if test $# -ne 1 -a $# -ne 2
then
  echo "Usage: "`basename "$0"`" <file> [define_name]"
  exit 1
fi

OUTPUT_FILE="`pwd`/${1}"
TEMP_FILE="${OUTPUT_FILE}.tmp"

#echo version...
#pwd
#echo $OUTPUT_FILE
#echo $TEMP_FILE

OLDPWD=`pwd`
cd ..

if test $# -eq 2
then
  DEFINE=${2}
else
  DEFINE=GIT_VERSION
fi

REV=$(git rev-parse HEAD)

test -z "$(git diff-index --name-only HEAD)" || REV="$REV-dirty"

echo "#define ${DEFINE} \"${REV}\"" > ${TEMP_FILE}
if test ! -f ${OUTPUT_FILE}
then
  echo "Generated ${OUTPUT_FILE} (${REV})"
  cp ${TEMP_FILE} ${OUTPUT_FILE}
  if test $? -ne 0; then exit 1; fi
else
  if ! cmp -s ${OUTPUT_FILE} ${TEMP_FILE}
  then echo "Regenerated ${OUTPUT_FILE} (${REV})"
    cp ${TEMP_FILE} ${OUTPUT_FILE}
    if test $? -ne 0; then exit 1; fi
  fi
fi

cd "${OLDPWD}"

rm ${TEMP_FILE}

exit $?
