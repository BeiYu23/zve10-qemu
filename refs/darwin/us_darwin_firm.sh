#!/bin/sh

#. /usr/bin/common.src

KEYWORD=$1
TARGET_DIR=$EXTRACT_ROOT/${KEYWORD}

FIRMUP_RESULT_OK=0
updater_log.sh "Darwin update START."
ud_darwin_firmup.elf $TARGET_DIR/darwin_boo.bin $TARGET_DIR/darwin_core.bin 
FIRMUP_RESULT=$?
echo "Darwin firmup result : $FIRMUP_RESULT"
if [ $FIRMUP_RESULT -eq $FIRMUP_RESULT_OK ] ; then
	echo "Darwin firmup --- OK"
	updater_log.sh "Darwin update END."
	exit 0