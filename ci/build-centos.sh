#!/usr/bin/env bash
TAGS=`git describe --abbrev=0 --tags`
VERSIONS=`echo $TAGS | sed 's/V//'`
RELEASE=`echo $CI_JOB_ID`

run_source() {
    ./util/makesrc $TAGS
}

run_build() {
    mkdir -p ~/rpmbuild/SOURCES/
    mv -f ~/futurehead-${VERSIONS}.tar.gz ~/rpmbuild/SOURCES/.
    scl enable llvm-toolset-7 devtoolset-7 'rpmbuild -ba futureheadcurrency.spec'
    scl enable llvm-toolset-7 devtoolset-7 'rpmbuild -ba futureheadcurrency-beta.spec'
}

run_update() {
    for file in ./futureheadcurrency*.in; do
	outfile="$(echo "${file}" | sed 's@\.in$@@')"
    
    	echo "Updating \"${outfile}\"..."

    	rm -f "${file}.new"
    	awk -v srch="@VERSION@" -v repl="$VERSIONS" -v srch2="@RELEASE@" -v repl2="$RELEASE" '{ sub(srch,repl,$0); sub(srch2,repl2, $0); print $0}' < ${file} > ${file}.new
    	rm -fr "${outfile}"
    	cat "${file}.new" > "${outfile}"
    	rm -f "${file}.new"
    	chmod 755 "${outfile}"
    done
}

run_update
run_source
run_build
