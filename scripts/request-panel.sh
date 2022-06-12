#!/bin/bash

HOSTNAME=$1
if [ "$HOSTNAME" == "" ]; then
  echo "Specify the hostname to transmit the requests to"
  exit 1
fi

# Panel size details https://developer.x-plane.com/article/screencoordinates/
REFS=""
REFS="${REFS} sim/graphics/view/panel_total_pnl_l sim/graphics/view/panel_total_pnl_b sim/graphics/view/panel_total_pnl_r sim/graphics/view/panel_total_pnl_t"
REFS="${REFS} sim/graphics/view/panel_visible_pnl_l sim/graphics/view/panel_visible_pnl_b sim/graphics/view/panel_visible_pnl_r sim/graphics/view/panel_visible_pnl_t"
REFS="${REFS} sim/graphics/view/panel_total_win_l sim/graphics/view/panel_total_win_b sim/graphics/view/panel_total_win_r sim/graphics/view/panel_total_win_t"
REFS="${REFS} sim/graphics/view/panel_visible_win_l sim/graphics/view/panel_visible_win_b sim/graphics/view/panel_visible_win_r sim/graphics/view/panel_visible_win_t"

# X-15 with 1024x1024 FBO target but panel is 1280x720?
# uf sim/graphics/view/panel_total_pnl_l 128
# uf sim/graphics/view/panel_total_pnl_b -448
# uf sim/graphics/view/panel_total_pnl_r 1152
# uf sim/graphics/view/panel_total_pnl_t 576
# uf sim/graphics/view/panel_visible_pnl_l 0
# uf sim/graphics/view/panel_visible_pnl_b 0
# uf sim/graphics/view/panel_visible_pnl_r 1280
# uf sim/graphics/view/panel_visible_pnl_t 720
# uf sim/graphics/view/panel_total_win_l 0
# uf sim/graphics/view/panel_total_win_b -560
# uf sim/graphics/view/panel_total_win_r 1280
# uf sim/graphics/view/panel_total_win_t 720
# uf sim/graphics/view/panel_visible_win_l 0
# uf sim/graphics/view/panel_visible_win_b 0
# uf sim/graphics/view/panel_visible_win_r 1280
# uf sim/graphics/view/panel_visible_win_t 720

# Zibo 737 with 2048x2048 FBO target but panel is 1280x720?
# uf sim/graphics/view/panel_total_pnl_l 0
# uf sim/graphics/view/panel_total_pnl_b -368
# uf sim/graphics/view/panel_total_pnl_r 1024
# uf sim/graphics/view/panel_total_pnl_t 656
# uf sim/graphics/view/panel_visible_pnl_l 0
# uf sim/graphics/view/panel_visible_pnl_b 0
# uf sim/graphics/view/panel_visible_pnl_r 1280
# uf sim/graphics/view/panel_visible_pnl_t 720
# uf sim/graphics/view/panel_total_win_l 0
# uf sim/graphics/view/panel_total_win_b -460
# uf sim/graphics/view/panel_total_win_r 1280
# uf sim/graphics/view/panel_total_win_t 820
# uf sim/graphics/view/panel_visible_win_l 0
# uf sim/graphics/view/panel_visible_win_b 0
# uf sim/graphics/view/panel_visible_win_r 1280
# uf sim/graphics/view/panel_visible_win_t 720


# Protocol for X-Plane ExtPanel plugin from https://github.com/vranki/ExtPlane
( for each in $REFS; do
  echo "sub $each"
done; sleep 1000s ) | nc $HOSTNAME 51000
