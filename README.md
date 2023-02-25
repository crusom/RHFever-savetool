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

## Adding points on action

#### The function is at 0x801eb10c

There are actually two places that adds points to [points\_mult](#whats-points_mult).
If user reacted to an action, then it's 0x801eaa28, otherwise it's the function below.

Both functions do the same, except the former does some other things besides it.

```C
/*
    *points_mult_things_p = 0x90649350 
    points_mult_things_p[0x108] = points_sum    (0x90649770)
    points_mult_things_p[0x109] = count_actions (0x90649774)
   *(undefined *)(points_mult_things_p + 0x10a) = times_pressed (0x90649778)
*/

void add_points_to_points_mult(int *points_mult_things_p,uint always_zero)
{
  /* checks byte at 0x90668d0d, this byte is set at the start of level.
     maybe this check is useful when we play demo?
  */
  if (*(char *)(iRam8032180c + 0xdd) == '\0') {
    return;
  }
  /* the condition is dead code actually as all calls to this function have 0 in the always_zero parameter */
  if (always_zero == 0) {
    always_zero = (uint)*(byte *)(points_mult_things_p + 0x10a) * 5 & 0xff;
  }
  *(undefined *)(points_mult_things_p + 0x10a) = 0;
  points_mult_things_p[0x108] = points_mult_things_p[0x108] + always_zero;
  points_mult_things_p[0x109] = points_mult_things_p[0x109] + 1;
  return;
}

```
#### Mult definitions

- points\_sum, points are added to it every action, if the key is pressed at the ideal moment. If it's too fast/too slow but, the number of points in an action is smaller (always divisible by 5, and max is 100).
- count\_actions increases by 1 every action
- times\_pressed counts buttons pressed between two actions


## Points calculate algorithm:

#### The function is at 0x801eb158

#### What's points\_mult?
points\_mult is just points gained in the currently played level. However, if this level was already beaten, then the old number of points is adjusted, with the new number of points (see [here](#set-points-algorithm)). Thus, it works de facto as a multiplier

```C
/* 
   I really couldn't make up a better name than that, sorry
   *points_mult_things_p = 0x90649350 
   points_mult_things_p[0x108] = points_sum
   points_mult_things_p[0x109] = count_actions
*/

uint calc_points_mult(int *points_mult_things_p)
{
  return (uint)(points_mult_things_p[0x108] * 100) / (uint)points_mult_things_p[0x109];
}
```

#### Definitions

[see this](#mult\-definitions)

## Save points_mult function

#### The function is at 0x800780c0

```C
void save_points_mult(char *save_buf,char param_2,short points_mult)
{
                    /* should recalculate? */
  save_buf[0xa2] = '\x01';
                    /* level status */
  save_buf[0xa3] = param_2;
                    /* saves points_mult */
  *(short *)(save_buf + 0xa4) = points_mult;
  return;
}
```


## Set points algorithm:

#### The function is at 0x8007842c, and it's called by 0x80081dd0

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
#### Definitions

[see this](#mult\-definitions)


## Flow calculate algorithm (in python for simplifiction):

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

## Addresses

**r13 is a variable having (probably always) 0x80328500 address**

| Address | Name | Parameters | Desc |
| ------- | ---- | ---------- | ---- |
| 0x80079460 | get\_save\_buffer() | int param1 (r13-0x6d0c) | returns 0x9066936c (save\_buffer) |
| 0x800782a4 | get\_points\_mult() | char \*save\_buffer | returns save\_buffer + 0xa4 (eg. points\_mult)|
| 0x80078288 | get\_should\_recalculate\_flag() | char \*save\_buffer | returns save\_buffer + 0x0a (eg. should\_recalculate\_flag) |
| 0x8007829c | get\_status\_of\_prev\_level() | char \*save\_buffer | returns save\_buffer + 0xa3 (eg. prev\_level\_status) |
| 0x800043c4 | memcpy() | void* dest, const void* src, size\_t size | [source](https://github.com/CelestialAmber/xenoblade/blob/76b251539176d6d29f8e5c9cf08a9c9749e178d1/src/PowerPC_EABI_Support/Runtime/__mem.c) |
| 0x80081858 | recalcuate\_flow() | idk | recalculates flow and sets it |
| 0x80081bac | save\_flow() | idk | does some magic and saves flow |
| 0x801eb0f8 | zero\_points\_mult | [char \*points\_mult\_things\_p](#whats-points_mult) | sets to 0 points\_sum, count\_actions and times\_pressed|

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
