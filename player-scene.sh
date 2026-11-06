#!/usr/bin/env bash
# Builds a player model set's Player.2kv6 from its 13 classic .kv6 parts,
# as the game's player template has it:
#
#   Body (torso joint)    Torso, Neck/Head, Neck/XmasHat (hidden), Shoulders/Arms
#   HipLeft, HipRight     Leg, LegRight
#   Crouch (hidden)       Body/TorsoCrouch, HipLeft/LegCrouch, HipRight/LegRightCrouch
#   FirstPerson (hidden)  UpperLeft/UpperArm, ForeLeft/Arm, UpperRight/UpperArmRight,
#                         ForeRight/ArmRight, half the size, as first person draws them
#   Dead (hidden)         Dead
#
# Every joint is a locked hinge where the game puts it (standing, third
# person; ClientPlayer.cpp); every part sits on its hinge as its .kv6 does on
# its pivot, so the game draws each exactly as it drew the .kv6. The hat is
# XmasHat.kv6 without its head, worn over the set's own head, a little larger
# so the two never share a face.
#
# usage: ./player-scene.sh GAME SET_DIR
#   GAME     the built game executable (it has --kv6-tools and --2kv6-tools)
#   SET_DIR  e.g. Resources/Models/Player/Zombie; writes SET_DIR/Player.2kv6
set -euo pipefail

game=$1
dir=${2%/}
here=$(cd "$(dirname "$0")" && pwd)
out="$dir/Player.2kv6"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

kv6() { "$game" --debug-tools --kv6-tools "$@"; }
scene() { "$game" --debug-tools --2kv6-tools "$@"; }

# Where a part goes on its hinge: its pivot's opposite.
at() {
	read -r x y z < <(kv6 get-pivot "$1")
	awk -v x="$x" -v y="$y" -v z="$z" 'BEGIN { printf "%g %g %g\n", -x, -y, -z }'
}

# The hat alone: XmasHat.kv6 is a head under a hat; its last three layers
# are the head.
hat="$tmp/XmasHat.kv6"
cp "$here/Resources/Models/Player/XmasHat.kv6" "$hat"
kv6 clear-box "$hat" 0 0 5 8 5 7 >/dev/null
kv6 resize "$hat" 9 6 5 >/dev/null

# Worn 5% larger about the middle of the head (voxels -2.5..2.5, -2.5..2.5,
# -5.5..-0.5 on the neck), which the hat's own place scales from.
read -r hx hy hz < <(at "$hat")
hatAt=$(awk -v x="$hx" -v y="$hy" -v z="$hz" \
	'BEGIN { s = 1.05; cz = -3; printf "%g %g %g\n", s * x, s * y, cz + s * (z - cz) }')

part() { echo "add-model $1 $dir/$2.kv6 --at $(at "$dir/$2.kv6")"; }

scene new "$out" --force >/dev/null
{
	echo "add-hinge / Body --at 0 0 -10"
	part Body Torso
	echo "add-hinge Body Neck --at 0 0 0"
	part Body/Neck Head
	echo "add-model Body/Neck $hat --at $hatAt"
	echo "set-scale Body/Neck/XmasHat 1.05"
	echo "hide Body/Neck/XmasHat"
	echo "add-hinge Body Shoulders --at 0 0 1"
	part Body/Shoulders Arms
	echo "add-hinge / HipLeft --at 2.5 0 -1"
	part HipLeft Leg
	echo "add-hinge / HipRight --at -2.5 0 -1"
	part HipRight LegRight

	echo "add-hinge / Crouch --at 0 0 0"
	echo "add-hinge Crouch Body --at 0 0 -5"
	part Crouch/Body TorsoCrouch
	echo "add-hinge Crouch HipLeft --at 2.5 -2.5 -0.5"
	part Crouch/HipLeft LegCrouch
	echo "add-hinge Crouch HipRight --at -2.5 -2.5 -0.5"
	part Crouch/HipRight LegRightCrouch
	echo "hide Crouch"

	echo "add-hinge / FirstPerson --at 14 0 -10"
	echo "set-scale FirstPerson 0.5"
	echo "add-hinge FirstPerson UpperLeft --at 4 0 0"
	part FirstPerson/UpperLeft UpperArm
	echo "add-hinge FirstPerson ForeLeft --at 4 0 13"
	part FirstPerson/ForeLeft Arm
	echo "add-hinge FirstPerson UpperRight --at -4 0 0"
	part FirstPerson/UpperRight UpperArmRight
	echo "add-hinge FirstPerson ForeRight --at -4 0 13"
	part FirstPerson/ForeRight ArmRight
	echo "hide FirstPerson"

	echo "add-hinge / Dead --at 0 30 8"
	part Dead Dead
	echo "hide Dead"

	for h in Body Body/Neck Body/Shoulders HipLeft HipRight Crouch Crouch/Body Crouch/HipLeft \
		Crouch/HipRight FirstPerson FirstPerson/UpperLeft FirstPerson/ForeLeft \
		FirstPerson/UpperRight FirstPerson/ForeRight Dead; do
		echo "lock $h"
	done
} | scene apply "$out"
echo "wrote $out"
