#!/bin/bash

set -Eeuo pipefail

usage() {
	echo -e \
	"Usage:\n" \
	"  $0 futurehead_node [daemon] [cli_options] [-l] [-v size]\n" \
	"    daemon\n" \
	"      start as daemon\n\n" \
	"    cli_options\n" \
	"      futurehead_node cli options <see futurehead_node --help>\n\n" \
	"    -l\n" \
	"      log to console <use docker logs {container}>\n\n" \
	"    -v<size>\n" \
	"      vacuum database if over size GB on startup\n\n" \
	"  $0 bash [other]\n" \
	"    other\n" \
	"      bash pass through\n" \
	"  $0 [*]\n" \
	"    *\n" \
	"      usage\n\n" \
	"default:\n" \
	"  $0 futurehead_node daemon -l"
}

OPTIND=1
command=()
IFS=' ' read -r -a TEMP_OPTS <<<"$@"
passthrough=()
db_size=0
log_to_cerr=0

if [ ${#TEMP_OPTS[@]} -lt 2 ]; then
	usage
	exit 1
fi

if [[ "${TEMP_OPTS[0]}" = 'futurehead_node' ]]; then
	unset 'TEMP_OPTS[0]'
	command+=("futurehead_node")
	shift;
	for i in "${TEMP_OPTS[@]}"; do
		case $i in
			"daemon" )
				command+=("--daemon")
				;;
			* )
				passthrough+=("$i")
				;;
		esac
	done
	for i in "${passthrough[@]}"; do
		if [[ "$i" =~ ^"-v" ]]; then
		        db_size=${i//-v/}
			echo "Vacuum DB if over $db_size GB on startup"
		elif [[ "$i" = '-l' ]]; then
			echo "log_to_cerr = true"
			command+=("--config")
			command+=("node.logging.log_to_cerr=true")
		else
		 	command+=("$i")
		fi
	done
elif [[ "${TEMP_OPTS[0]}" = 'bash' ]]; then
	unset 'TEMP_OPTS[0]'
	echo -e "EXECUTING ${TEMP_OPTS[*]}\n"
	exec "${TEMP_OPTS[@]}"
	exit 0;
else
	usage
	exit 1;
fi

network="$(cat /etc/futurehead-network)"
case "${network}" in
	live|'')
	network='live'
	dirSuffix=''
	;;
	beta)
	dirSuffix='Beta'
	;;
	test)
	dirSuffix='Test'
	;;
esac

raidir="${HOME}/RaiBlocks${dirSuffix}"
futureheaddir="${HOME}/Futurehead${dirSuffix}"
dbFile="${futureheaddir}/data.ldb"

if [ -d "${raidir}" ]; then
	echo "Moving ${raidir} to ${futureheaddir}"
	mv "$raidir" "$futureheaddir"
else
	mkdir -p "${futureheaddir}"
fi

if [ ! -f "${futureheaddir}/config-node.toml" ] && [ ! -f "${futureheaddir}/config.json" ]; then
	echo "Config file not found, adding default."
	cp "/usr/share/futurehead/config/config-node.toml" "${futureheaddir}/config-node.toml"
	cp "/usr/share/futurehead/config/config-rpc.toml" "${futureheaddir}/config-rpc.toml"
fi

if [[ "${command[1]}" = "--daemon" ]]; then
	if [[ $db_size -ne 0 ]]; then
		if [ -f "${dbFile}" ]; then
			dbFileSize="$(stat -c %s "${dbFile}" 2>/dev/null)"
			if [ "${dbFileSize}" -gt $((1024 * 1024 * 1024 * db_size)) ]; then
				echo "ERROR: Database size grew above ${db_size}GB (size = ${dbFileSize})" >&2
				futurehead_node --vacuum
			fi
		fi
	fi
fi
echo -e "EXECUTING: ${command[*]}\n"
exec "${command[@]}"
