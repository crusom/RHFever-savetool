#### DISCLAIMER: I used European ROM with SHA-1 b15f37a33a16dbae09b220dae5cfc4d665ee528c, other ROMs may have some code differences

# RHF analysis

## Intro

I was interested in the internals of this game, and save files (if unencrypted) are relatively low-hanging fruit.

One of the problems with this game is the ambiguity of the flow system. What does it even mean?   

All I could find is people saying they don't know [here](https://gamefaqs.gamespot.com/boards/609557-rhythm-heaven-fever/62377586), [here](https://rhythmheaven.fandom.com/wiki/Rhythm_Game) and [here](https://www.reddit.com/r/rhythmheaven/comments/c5bx0d/how_does_flow_work_in_fever/)

In the Reddit thread, a responder even says

> I don’t know if anybody knows how the Flow Meter works.
> Thankfully, though, it doesn’t impact the game in any way whatsoever, so don’t worry about it.

Oh, that's encouraging. If really nobody knows that, then let's dive in into our niche and solve the mystery!

## Methods

Dolphin's debugger turned out pretty good, it supports breakpoints, watchpoints, memory view, logging, there's even a cheat manager!
[Here's a great, exhaustive tutorial for all of this](https://tasvideos.org/Forum/Topics/18555?CurrentPage=1&Highlight=444464)

And of course the RE necessity, Ghidra.

The process consisted of looking in Ghidra discomp for interesting functions/variables and setting up breakpoints in the Dolphin's debugger. Although it's enough for a simple analysis, it's not enough for anything more breadth. I wish we will do a whole decomp someday (maybe after [tengoku](https://github.com/arthurtilly/rhythmtengoku)).

Comparing two binary files is as easy as doing:

`
$ diff <(xxd Riq1.dat) <(xxd Riq2.dat)
`

# Saves

## General save structure:

*  first  profile:  0x000-0x27f
*  second profile:  0x280-0x4ff
*  third  profile:  0x500-0x77f
*  fourth  profile:  0x780-0x9ff
*  crc32 checksum: 0xa00-0xa03
*  const: 0xa04-0xa07
*  padding: 0xa08-0xa1f

## Profile structure (addresses relative to the beginning of the profile):

| Address | Data type | Desc |
| ------- | --------- | ---- |
|  0x09 |byte| how many level groups are unlocked, counting from 0x00|
|  0x0a |byte| last played level. 0x00 rhythm test, 0x01 rhythm toys, 0x02 endless games, 0x03 extra games, 0x04 credits, normal levels start at 0x05 (so after the monkey golf, this initializes to 0x05)|
|  0x0b-0x3c |byte| level statuses |
|  0x3d |byte| indicates if the first game has been beaten, 1 if started, 0 if not (if yes, it loades the leftmost column of additional games)|
|  0x3e-0xa1 |word| level points|
|  0xa2 |byte| tells the game that it should recalculate things, like adjust the last played game score, with the points multiplier|
|  0xa3 |byte| status of the prev played level (possibly?)|
|  0xa4 |word| points multiplier|
|  0xa6-0xa9 |word| seem to be const in all saves|
|  0xaa-0xdd |word| I actually don't know, it's generated at the creation of the save, and probably completedly changes after winning a group. Maybe it's encoded.|
|  0xec |dword| wakeup caller points|
|  0xf0 |dword| much monk points|
|  0xf4 |dword| lady cupid points|
|  0xf8 |dword| mr upbeat points|
|  0xfc |dword| endless remix points|
|  0x0fd-0x24f |byte| unknown or zeroes|
|  0x250 |byte| profile type (me, bro, sis etc.), values 0x00-0x0e |
|  0x251 |byte| first time in cafe|
|  0x252 |byte| barista congratulates perfect campain done|
|  0x253 |byte| first time in dual play|
|  0x254 |byte| voice language, 0 if English, 1 if Japanese|
|  0x255-0x27f |byte| unknown or zeroes|

## Level statuses:

*  0x00 - unavailable
*  0x01 - unlocked in this column
*  0x02 - unlocked to play
*  0x03 - ok
*  0x04 - superb
*  0x05 - perfect

## Points calculate algorithm:

#### The function is at 0x801eb158

```C
/* points_mult = 0x90649350 
   points_mult[0x108] = points_scored
   points_mult[0x109] = len_actions
*/

uint calc_points_mult(int *points_mult)
{
  return (uint)(points_mult[0x108] * 100) / (uint)points_mult[0x109];
}
```
- len\_actions increases by 1 every action

- points\_scored increases up to 100, if the key is pressed at the ideal moment. If it's too fast/too slow but, the number of points in an action is smaller (always divisible by 5).

- points\_mult is just points gained in the currently played level. However, if this level was already beaten, then the old number of points is adjusted, with the new number of points. Thus, it works de facto as a multiplier

## Set points algorithm:

#### The function is at 0x8007842c 

```C
void set_level_points(char *save_buf, uint level_number_param, int points_mult)
{
  uint uVar1;
  int level_points;
  short sVar2;
  uint level_number;
  
  level_number = level_number_param & 0xff;
  /* 0x3e is offset to the points */
  level_points = (int)*(short *)(save_buf + level_number * 2 + 0x3e);
  if (level_points < 0) {
    *(short *)(save_buf + level_number * 2 + 0x3e) = (short)points_mult;
    return;
  }
  uVar1 = points_mult - level_points >> 0x1f;
  if ((int)((uVar1 ^ points_mult - level_points) - uVar1) < 300) {
    sVar2 = *(short *)(save_buf + level_number * 2 + 0x3e);
    if (level_points < points_mult) {
      sVar2 = (short)points_mult;
    }
    *(short *)(save_buf + level_number * 2 + 0x3e) = sVar2;
    return;
  }
  *(short *)(save_buf + level_number * 2 + 0x3e) = (short)((points_mult + level_points) / 2);
  return;
}
```

## Flow calculate algorithm (in python):

#### The function is at 0x8007848c

```python
  for points in level_points:
    if points != 0xffff:
      levels_len += 1
      points_sum += points + 50

  flow = (levels_len - 1) * 70
  flow = flow / 50
  points_sum = points_sum / (levels_len * 100)
  if points_sum < 0:
    levels_len = 0
  else:
    levels_len = min(points_sum, 100)
  
  levels_len = levels_len * (flow + 50)
  points_sum = levels_len / 100
  flow = points_sum + 20
  
  return flow
```

# Notes about data.bin and keys

## Savefiles

### Riq.dat

Riq.dat is our plain savefile.

#### Dolphin

It's stored in ~/.local/share/dolphin-emu/Wii/title/\<game-id-first-8-digits\>/\<game-id-last-8-digits\>/data/Riq.dat

find game-id in dolphin-\>game properties-\>info

just edit it and have fun

#### Wii hardware

see below section

### data.bin

data.bin is a compressed file which contains *a Header, followed by a Bk Header and a set of files contained in a files section, and finally a footer* full format description [here](https://wiibrew.org/wiki/Savegame_Files)

It's just a packed savefile.

#### How to unpack/pack data.bin?

- use tachtig (unpack) and twintig in Segher's Wii.git on linux ([wiki](https://wiibrew.org/wiki/Segher%27s_Wii.git), [github](https://github.com/MasterofGalaxies/wii), [release download](https://github.com/MasterofGalaxies/wii/releases/download/2022/wii.zip))

- or FE100 on windows ([wiki](https://wiibrew.org/wiki/FE100), [source](http://www.tepetaklak.com/data/FE100-0.2b-src.rar), [download](http://www.tepetaklak.com/data/FE100-v0.23b.rar))

- or make your own tool, its just AES128-CBC after all

I make some changes to tachtig code, now it saves all the keys in default/, which twintig uses to pack the binary again. So all you have to do is **add your NG-priv here**.

**If you use Dolphin and want to pack data.bin, then copy default-dolphin/ to default/**

### keys keys keys

to extract data.bin we need this shared keys:

- SD key (ab01b9d8e1622b08afbad84dbfc2a55d), 
- SD IV (216712e6aa1f689f95c5a22324dc6a98),
- MD5 blanker (0e65378199be4517ab06ec22451a5793),

[Wii security](https://wiibrew.org/wiki/Wii_security)

[Wii keys](https://hackmii.com/2008/04/keys-keys-keys/)
> When copying a save game from a Wii system memory to an SD card (in "Data Management"), it encrypts it with an AES key known to all consoles (SD-key). This serves only to keep prying eyes from reading a save game file. In crypto terminology, the SD-key is a "shared secret".

> The Wii then signs the file on the SD card with its private (ECC) key. This is to prevent anyone from modifying the save file while it is on the SD card.

> If someone shares save games to another Wii using an SD card, the Wii will be able to decrypt it using the shared secret. However, it has no way of checking the Wii's signature, because it doesn't know the other console's public key. To solve this problem, the save game also contains a copy of the Wii's unique public key -- the one that matches the private key used to sign the save file. (The copy of the Wii's public key is called a 'certificate'.) "

to pack data.bin again we need NG-keys:

- NG-id (your consoles id)
- NG-key-id
- NG-mac (your consoles wifi adapters mac address)
- NG-sig (your consoles elliptical curve crypt. public key)
- NG-priv (private key)

**All of it is given in a data.bin except of the private key.**

### NG-keys dolphin

https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/IOS/IOSC.cpp

they're already in default-dolphin directory.

### NG-keys Wii hardware

you probably need to dump the NAND, then [extract it](http://www.wiibrew.org/wiki/NandExtract), and get the keys from [keys.bin](https://wiibrew.org/wiki/Bootmii/NAND_dump_format#keys.bin)

FE100 can nicely get all the keys from the given keys.bin and savefile.


# TODO
- [ ] analyze dual play
- [x] analyze function adjusting points with points\_mult (probably 0x8007842c)
- [ ] add flow changing in savetool
