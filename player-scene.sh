#!/usr/bin/env bash
# Builds a player model set's Player.2kv6 from its 13 classic .kv6 parts, as
# the player template has them:
#
#   Player                        the player: turns with where they face
#   ├─ UpperBody                  top of the torso: the torso, head and arms hang from it
#   │  ├─ Torso                   Torso.kv6
#   │  ├─ Neck                    the head nods about it, with the aim
#   │  │  ├─ Head                 Head.kv6
#   │  │  └─ SantaHat   [hidden]  worn over Head at Christmas
#   │  └─ Shoulders               tilt with the aim
#   │     └─ ArmsHolding          Arms.kv6: both arms holding a tool, third person
#   ├─ LeftHip ─ LeftLeg          Leg.kv6
#   └─ RightHip ─ RightLeg        LegRight.kv6
#   Crouching           [hidden]  the same joints, crouched
#   ├─ UpperBody ─ Torso          TorsoCrouch.kv6
#   ├─ LeftHip ─ LeftLeg          LegCrouch.kv6
#   └─ RightHip ─ RightLeg        LegRightCrouch.kv6
#   JointedArms         [hidden]  bent by first person (half size), and the corpse's
#   ├─ LeftShoulder ─ LeftUpperArm ─ LeftElbow ─ LeftForearm      UpperArm, Arm.kv6
#   └─ RightShoulder ─ RightUpperArm ─ RightElbow ─ RightForearm  UpperArmRight, ArmRight
#   Fallen              [hidden]  shown when ragdolls are off
#   └─ Corpse                     Dead.kv6
#
# The game places every joint itself and draws each part on its own hinge
# (Client/PlayerModels.cpp), so the hinges' places here only show the parts
# together: the jointed arms are laid out in a T-pose to be edited. Each part
# sits on its hinge as its .kv6 sat on its pivot, so the game draws it as it
# drew the .kv6. Hinges and parts are locked: the game finds them by name.
# The scene frame is the game's model frame: +Y the front, +X the player's
# left, -Z up.
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
hat="$tmp/SantaHat.kv6"
cp "$here/Resources/Models/Player/XmasHat.kv6" "$hat"
kv6 clear-box "$hat" 0 0 5 8 5 7 >/dev/null
kv6 resize "$hat" 9 6 5 >/dev/null

# Worn 5% larger about the middle of the head (voxels -2.5..2.5, -2.5..2.5,
# -5.5..-0.5 on the neck), so the two never share a face.
read -r hx hy hz < <(at "$hat")
hatAt=$(awk -v x="$hx" -v y="$hy" -v z="$hz" \
	'BEGIN { s = 1.05; cz = -3; printf "%g %g %g\n", s * x, s * y, cz + s * (z - cz) }')

# add-model PARENT PART NAME: the set's PART.kv6 as NAME, on its pivot.
part() { echo "add-model $1 $dir/$2.kv6 --name $3 --at $(at "$dir/$2.kv6")"; }

# Where an elbow hangs from its upper arm: the arm's pivot, 10 voxels down
# it, the length first person bends it by (0.5 at 0.05 a voxel).
elbow() {
	read -r x y z < <(kv6 get-pivot "$dir/$1.kv6")
	awk -v x="$x" -v y="$y" -v z="$z" 'BEGIN { printf "%g %g %g\n", x, y, z + 10 }'
}

scene new "$out" --force >/dev/null
{
	echo "add-hinge / Player"
	echo "add-hinge Player UpperBody --at 0 0 -10"
	part Player/UpperBody Torso Torso
	echo "add-hinge Player/UpperBody Neck"
	part Player/UpperBody/Neck Head Head
	echo "add-model Player/UpperBody/Neck $hat --name SantaHat --at $hatAt"
	echo "set-scale Player/UpperBody/Neck/SantaHat 1.05"
	echo "hide Player/UpperBody/Neck/SantaHat"
	echo "add-hinge Player/UpperBody Shoulders --at 0 0 1"
	part Player/UpperBody/Shoulders Arms ArmsHolding
	echo "add-hinge Player LeftHip --at 2.5 0 -1"
	part Player/LeftHip Leg LeftLeg
	echo "add-hinge Player RightHip --at -2.5 0 -1"
	part Player/RightHip LegRight RightLeg

	echo "add-hinge / Crouching"
	echo "add-hinge Crouching UpperBody --at 0 0 -5"
	part Crouching/UpperBody TorsoCrouch Torso
	echo "add-hinge Crouching LeftHip --at 2.5 -2.5 -0.5"
	part Crouching/LeftHip LegCrouch LeftLeg
	echo "add-hinge Crouching RightHip --at -2.5 -2.5 -0.5"
	part Crouching/RightHip LegRightCrouch RightLeg
	echo "hide Crouching"

	# Half size, as first person draws them; shoulders on the body's, the
	# arms out to the sides.
	echo "add-hinge / JointedArms"
	echo "set-scale JointedArms 0.5"
	echo "add-hinge JointedArms LeftShoulder --at 9 0 -19"
	echo "set-rotation JointedArms/LeftShoulder 0 90 0"
	part JointedArms/LeftShoulder UpperArm LeftUpperArm
	echo "add-hinge JointedArms/LeftShoulder/LeftUpperArm LeftElbow --at $(elbow UpperArm)"
	part JointedArms/LeftShoulder/LeftUpperArm/LeftElbow Arm LeftForearm
	echo "add-hinge JointedArms RightShoulder --at -9 0 -19"
	echo "set-rotation JointedArms/RightShoulder 0 -90 0"
	part JointedArms/RightShoulder UpperArmRight RightUpperArm
	echo "add-hinge JointedArms/RightShoulder/RightUpperArm RightElbow --at $(elbow UpperArmRight)"
	part JointedArms/RightShoulder/RightUpperArm/RightElbow ArmRight RightForearm
	echo "hide JointedArms"

	echo "add-hinge / Fallen --at 0 -30 8"
	part Fallen Dead Corpse
	echo "hide Fallen"

	for n in Player Player/UpperBody Player/UpperBody/Torso Player/UpperBody/Neck \
		Player/UpperBody/Neck/Head Player/UpperBody/Neck/SantaHat Player/UpperBody/Shoulders \
		Player/UpperBody/Shoulders/ArmsHolding Player/LeftHip Player/LeftHip/LeftLeg \
		Player/RightHip Player/RightHip/RightLeg \
		Crouching Crouching/UpperBody Crouching/UpperBody/Torso Crouching/LeftHip \
		Crouching/LeftHip/LeftLeg Crouching/RightHip Crouching/RightHip/RightLeg \
		JointedArms JointedArms/LeftShoulder JointedArms/LeftShoulder/LeftUpperArm \
		JointedArms/LeftShoulder/LeftUpperArm/LeftElbow \
		JointedArms/LeftShoulder/LeftUpperArm/LeftElbow/LeftForearm \
		JointedArms/RightShoulder JointedArms/RightShoulder/RightUpperArm \
		JointedArms/RightShoulder/RightUpperArm/RightElbow \
		JointedArms/RightShoulder/RightUpperArm/RightElbow/RightForearm \
		Fallen Fallen/Corpse; do
		echo "lock $n"
	done
} | scene apply "$out"

# What each part is, for whoever opens the scene.
cat >"$tmp/template.txt" <<'EOF'
Player model template. The game finds every part by its path and places
every joint itself; a part is drawn on its own hinge, as its classic .kv6
was on its pivot. Give a part new voxels or move it on its hinge (its
pivot); its name and turn are the template's.

Part                                   Classic file        Used in
Player/UpperBody/Torso                 Torso.kv6           third person, first person, corpse
Player/UpperBody/Neck/Head             Head.kv6            third person, corpse
Player/UpperBody/Neck/SantaHat         (XmasHat.kv6)       over Head, 20-27 December
Player/UpperBody/Shoulders/ArmsHolding Arms.kv6            third person
Player/LeftHip/LeftLeg                 Leg.kv6             third person, first person, corpse
Player/RightHip/RightLeg               LegRight.kv6        third person, first person, corpse
Crouching/UpperBody/Torso              TorsoCrouch.kv6     crouching, third and first person
Crouching/LeftHip/LeftLeg              LegCrouch.kv6       crouching, third and first person
Crouching/RightHip/RightLeg            LegRightCrouch.kv6  crouching, third and first person
JointedArms/.../LeftUpperArm           UpperArm.kv6        first person
JointedArms/.../LeftForearm            Arm.kv6             first person, corpse (whole arm)
JointedArms/.../RightUpperArm          UpperArmRight.kv6   first person
JointedArms/.../RightForearm           ArmRight.kv6        first person, corpse (whole arm)
Fallen/Corpse                          Dead.kv6            dead, when ragdolls are off

Axes: +Y is the front, +X the player's left, -Z up.
EOF
scene meta-set "$out" Template --from "$tmp/template.txt" >/dev/null
echo "wrote $out"
