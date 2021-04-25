#
# Prerequisites: libasound-dev, libgpiod-dev
#

midi2gpiod: midi2gpiod.c
	clang -o midi2gpiod midi2gpiod.c -lasound -lgpiod
