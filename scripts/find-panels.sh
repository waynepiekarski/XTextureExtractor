#!/bin/bash

# https://developer.x-plane.com/manuals/planemaker/
# X-Plane searches in either cockpit_3d/-PANELS- or cockpit/-PANELS-
# Panel_General.png
# Panel_Airliner.png
# Panel_Fighter.png
# Panel_Glider.png
# Panel_Helo.png
# Panel_Autogyro.png
# Panel_General_IFR.png
# Panel_Autogyro_Twin.png
# Panel_Fighter_IFR.png

DIR="$1"
if [[ "${DIR}" == "" ]]; then
    echo "Specify X-Plane directory"
    exit 1
fi

function dump_panel() {
    DIR="$(dirname "$1")"
    echo "ACF = $1, DIR = ${DIR}"
    IFS=$'\n'
    # Case insensitive search
    # Some aircraft don't use cockpit_3d and so need to fallback to cockpit if needed
    FILES=($(find "${DIR}" -iname "*.png" | grep -i "cockpit_3d/-PANELS-" | grep -v -i preview | grep -v "(2)"; find "${DIR}" -iname "*.png" | grep -i "cockpit/-PANELS-" | grep -v -i preview | grep -v "(2)"))
    unset IFS
    for each in "${FILES[@]}"; do
	SIZE="$(identify -format "%G" "${each}")"
	echo "    ${SIZE} == ${each}"
    done
}
export -f dump_panel

find "${DIR}" -name "*.acf" -exec bash -c 'dump_panel "{}"' \;

