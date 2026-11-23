#include "gibberish_ngram.h"

namespace formant::detail {

std::string_view builtinEnUsNgramTable() {
    static constexpr std::string_view kTable = R"FORMANT_NGRAM(
# formant gibberish n-gram table v1
lang en-us
source CMUdict cmudict.dict (Carnegie Mellon University, BSD-2-Clause)
license BSD-2-Clause (CMUdict)
mapping arpa-to-formant-v1
note Derived phoneme transition statistics; not original dictionary entries.
begin lengths
6 27276
5 25996
7 21081
4 16424
8 15249
9 9248
3 6834
10 5138
11 2857
12 1523
2 961
1 42
end lengths
begin starts
k 12842
s 12285
b 9709
m 9416
p 7709
d 7634
r 7359
h 6662
f 5588
l 5453
g 4954
t 4860
i 3890
w 3833
uh 3325
n 3176
a 2908
e 2867
sh 2493
v 2421
j 2160
aa 1929
y 1367
o 1283
ch 1264
ay 1130
z 957
or 899
th 643
ee 583
er 391
ow 347
oo 101
zh 91
dh 72
oy 28
end starts
begin ends
z 20403
n 14784
ee 13190
s 11208
er 10061
t 9198
d 8576
l 7350
uh 6516
ng 5824
k 5299
o 5285
m 2516
r 1833
ay 1429
p 1109
g 1067
f 1001
v 855
oo 766
sh 712
j 692
th 667
ch 630
b 430
aa 409
or 246
ow 235
oy 176
dh 40
e 36
h 25
zh 25
i 18
y 7
w 6
a 5
end ends
begin unigrams
uh 68219
n 58548
s 48763
l 48658
i 48499
t 47123
r 45068
k 41768
ee 34242
d 31731
m 28966
er 28662
z 27529
e 26691
aa 24510
ay 24375
a 21492
b 21045
p 19241
o 18943
f 13562
g 13439
oo 11954
or 11266
v 10514
ng 9795
h 9226
w 8830
sh 8339
j 6203
y 5018
ch 4856
ow 3333
th 2860
oy 1273
dh 574
zh 557
end unigrams
begin bigrams
uh	n:20339 l:11411 s:4847 m:4259 t:3772 k:3488 b:2250 z:1897 d:1816 p:1403 f:1296 g:1147 v:959 j:669 ng:436 sh:306
i	ng:7497 n:7358 k:5342 s:4787 l:3620 t:3106 d:2464 z:2416 m:1850 r:1610 v:1346 p:1227 g:1208 sh:1144 f:948 j:772
r	ee:6061 uh:5795 i:4890 ay:3167 o:2837 e:2811 a:2721 aa:2041 t:1783 oo:1429 d:1399 k:923 m:828 n:819 s:681 or:642
n	t:5597 d:5201 uh:4553 z:4232 s:3508 i:2960 ee:2742 er:1729 o:1500 ay:1234 e:1089 aa:1085 b:714 a:709 j:688 f:651
l	ee:5526 uh:5404 i:5042 ay:3048 er:2157 a:2094 d:2074 z:2059 e:1888 aa:1596 o:1573 t:1225 oo:1113 or:870 m:773 s:685
t	uh:5266 i:5181 s:4724 er:3981 r:3883 ee:2861 e:1906 ay:1888 o:1353 aa:1313 a:1079 l:756 or:693 oo:631 m:365 w:333
s	t:9320 uh:4982 i:3574 k:3506 ee:2303 p:1906 e:1837 ay:1302 er:1175 l:1112 aa:905 a:782 m:745 o:680 oo:595 w:571
k	uh:5541 s:3548 aa:3267 r:2460 ee:2270 l:2101 o:2084 a:1897 er:1878 t:1738 i:1729 or:1301 w:1260 ay:1173 e:819 y:661
e	n:4586 l:4057 r:3779 k:2788 s:2442 t:1934 m:1390 d:1323 v:647 p:609 g:508 f:484 z:450 sh:322 b:306 ng:255
m	uh:6859 i:2679 aa:2185 a:1769 e:1750 p:1622 ay:1515 er:1355 b:1085 ee:1042 o:899 z:838 or:706 y:342 oo:338 s:220
aa	r:6343 n:3973 l:2430 k:1780 m:1502 t:1326 s:1204 p:869 d:841 b:634 g:556 v:478 z:386 f:315 ng:258 ch:230
d	i:4135 uh:3547 ee:2005 er:1836 e:1491 z:1360 r:1269 ay:1235 aa:896 o:860 oo:572 l:563 a:531 or:512 m:294 s:287
ay	t:3916 n:2954 z:2044 d:1783 l:1605 k:1337 sh:1260 s:1242 m:881 v:752 uh:687 b:656 er:580 r:567 p:511 g:423
a	n:5330 k:2455 l:1984 m:1742 s:1652 t:1377 ng:952 d:926 p:768 g:691 b:641 f:624 r:462 sh:450 v:374 ch:315
b	uh:2865 er:2537 r:2428 aa:1502 e:1478 i:1425 l:1300 ee:1199 a:1197 ay:913 o:850 or:695 oo:644 y:285 z:258 ow:250
ee	uh:2600 n:2542 z:2480 l:1343 t:1278 d:1059 s:997 k:985 m:818 o:798 v:691 aa:488 p:487 g:465 er:434 b:425
p	r:2560 er:1729 uh:1647 l:1388 e:1349 aa:1312 i:1297 ee:1071 a:921 ay:795 s:755 o:703 t:635 or:520 oo:264 y:242
er	z:3512 d:1442 t:1231 n:1037 s:1037 g:944 m:869 i:812 uh:796 k:776 ee:756 ay:664 l:659 b:541 v:367 e:304
o	l:2540 n:1614 z:1290 s:1026 t:925 m:854 v:853 k:834 d:745 b:617 p:424 g:304 sh:249 f:215 h:167 uh:166
f	i:1337 r:1282 er:1155 uh:1125 l:988 ay:958 e:903 or:808 ee:777 s:575 aa:550 t:472 a:443 o:341 oo:265 y:216
g	r:2130 uh:2046 er:1024 aa:874 l:871 i:734 o:669 ay:562 a:558 z:420 n:380 e:351 oo:313 or:296 ee:264 w:225
or	r:5951 l:1249 f:712 n:500 s:491 t:402 k:284 ng:269 d:237 g:174 z:140 th:118 m:87 p:78 b:74 sh:68
oo	l:1108 r:1030 d:1025 t:974 n:823 k:778 m:724 z:676 s:669 p:501 b:412 uh:303 g:233 sh:218 i:205 v:185
v	er:1612 i:1568 uh:1294 ee:821 e:790 a:716 ay:702 aa:529 o:302 z:295 l:222 r:149 d:118 or:103 y:101 oy:80
h	aa:1266 a:1201 e:1071 ay:984 uh:768 o:716 i:644 or:532 er:409 ow:388 w:378 ee:306 oo:273 y:202 oy:37 r:14
w	i:1945 ay:1455 e:1188 or:846 aa:832 er:687 ee:598 uh:491 oo:446 o:144 a:142 oy:26 ow:17
sh	uh:3116 i:728 ee:474 e:372 aa:333 er:299 oo:285 ay:278 a:255 o:181 r:179 or:170 n:164 m:159 l:150 t:130
z	uh:1483 i:1170 ee:649 er:613 d:453 ay:314 e:311 aa:298 m:257 o:231 a:220 b:216 l:194 oo:157 n:127 w:102
j	uh:1079 i:878 e:764 ee:657 er:484 oo:301 a:261 aa:219 ay:170 o:166 d:147 or:100 oy:78 m:52 w:25 y:24
y	oo:2561 uh:1073 aa:327 er:286 o:182 e:175 a:130 or:94 i:59 ee:53 ay:42 ow:17
ch	i:609 er:604 ee:544 uh:528 e:462 a:250 oo:229 ay:205 aa:192 t:119 o:87 m:61 or:54 l:40 k:35 r:30
ng	k:1506 g:810 z:503 er:348 t:172 l:142 h:79 s:63 uh:58 d:45 b:35 m:33 i:32 w:23 f:21 sh:18
ow	n:808 t:531 s:406 er:225 d:148 z:139 l:119 m:97 r:94 uh:93 th:68 b:62 ch:51 k:38 i:37 g:31
th	uh:354 ee:279 er:210 r:202 i:177 s:137 or:128 e:104 aa:77 a:66 ay:66 w:60 l:57 o:32 k:30 m:30
oy	n:204 l:141 d:129 s:103 z:86 t:83 uh:68 er:59 k:27 i:25 m:21 ch:20 b:19 r:19 f:14 o:11
zh	uh:242 er:89 i:46 ee:26 aa:22 w:21 oo:16 e:13 ay:12 d:12
dh	er:308 ee:52 uh:37 e:27 i:26 d:18 z:15 ay:12
end bigrams
begin trigrams
uh n	z:2169 t:2001 d:1414 s:1208 uh:641 i:541 ay:447 b:345 ee:302 e:283 aa:188 f:184
s t	r:1223 uh:964 i:843 er:831 ay:599 ee:496 s:480 e:457 aa:449 a:406 o:289 or:217
m uh	n:3448 k:1056 l:521 t:349 s:291 g:214 z:124 d:107 m:62 j:43 ch:37 f:37
uh l	z:1024 ay:814 i:768 ee:720 er:466 uh:425 d:388 t:320 e:269 aa:200 m:170 s:162
t i	ng:1246 d:1161 k:742 v:458 n:299 s:269 l:197 r:131 m:114 t:94 sh:84 p:75
k uh	l:1399 n:1381 m:674 t:337 s:266 v:170 p:151 b:130 z:116 d:86 k:48 ch:40
s uh	n:1883 l:782 b:363 m:320 z:241 p:190 k:186 s:182 t:171 f:136 d:125 v:53
r uh	n:1018 s:587 l:545 m:505 t:391 k:353 f:261 d:251 b:208 p:201 g:157 z:153
aa r	t:946 d:695 k:665 m:326 l:306 n:294 b:292 ee:253 s:232 uh:227 o:196 g:187
l i	ng:1240 n:882 s:544 k:486 t:427 m:177 p:174 f:161 v:129 sh:125 z:112 j:104
t uh	n:1696 l:827 m:374 d:316 s:273 b:247 k:203 f:162 t:118 g:114 z:109 p:80
i n	t:844 s:796 d:587 z:417 uh:356 i:319 f:229 ee:212 j:207 k:199 er:176 v:173
or r	t:732 ee:548 uh:530 d:510 n:439 m:386 s:332 i:242 k:190 g:160 f:153 b:133
a n	d:1349 t:668 uh:666 s:559 i:288 z:285 j:126 ee:102 ch:95 y:80 k:67 er:61
l uh	n:1244 s:586 t:473 m:443 k:346 j:244 b:172 d:172 v:161 f:156 g:150 z:130
r i	k:862 s:641 ng:457 n:397 t:366 p:254 j:220 l:193 m:183 z:182 g:180 d:175
n t	s:742 uh:593 er:577 i:485 r:390 ee:348 e:242 ay:205 l:149 o:122 a:83 aa:74
t r	uh:849 a:538 ay:506 i:496 ee:417 o:286 aa:255 e:242 oo:148 or:64 ow:52 oy:27
e n	t:936 d:709 uh:449 s:380 i:265 z:212 er:171 ch:167 ee:155 sh:155 k:76 r:69
d i	s:929 ng:508 k:419 d:335 n:248 l:221 f:220 t:177 m:172 v:169 p:162 sh:110
r ee	uh:700 z:434 s:367 n:361 t:280 d:268 k:263 m:263 p:179 v:174 l:170 i:142
n uh	l:797 s:702 n:471 t:363 m:286 k:193 z:157 b:136 f:117 v:102 g:100 d:91
n d	er:756 uh:713 i:487 z:307 r:279 ee:241 o:155 l:134 ay:127 e:126 s:81 or:70
i s	t:1461 uh:495 k:358 i:324 p:219 ee:135 e:131 l:96 m:73 ay:63 b:63 er:54
s i	z:767 n:441 ng:417 s:275 k:256 l:255 m:220 t:208 v:175 d:124 r:88 g:86
e r	uh:986 ee:889 i:500 o:222 z:116 d:69 t:69 l:56 m:51 er:44 ay:43 n:40
s k	ee:1119 uh:355 r:281 o:273 aa:199 i:170 ay:150 w:127 a:125 or:102 er:90 e:83
k aa	r:797 n:711 m:407 l:276 t:170 p:150 s:145 k:141 v:62 z:62 ng:57 b:48
i k	s:925 uh:784 l:282 t:233 er:168 aa:130 ee:88 y:87 w:86 ay:80 i:80 or:79
sh uh	n:2328 l:261 s:111 z:62 t:48 m:43 k:35 p:28 f:21 b:20 v:17 w:13
aa n	t:556 d:422 uh:411 o:333 s:290 i:234 z:209 ee:204 k:89 er:77 aa:72 f:70
d uh	n:870 l:749 k:282 m:249 s:161 b:159 t:99 d:87 f:82 z:73 v:57 p:49
n s	uh:574 t:533 i:398 k:362 ee:260 er:174 e:133 ay:112 p:84 l:73 m:47 f:40
uh t	ee:459 s:429 i:427 er:271 uh:228 ay:221 r:186 e:156 or:120 aa:102 l:83 a:77
e l	uh:713 ee:347 i:298 o:235 d:206 t:189 z:172 m:158 er:130 f:106 s:98 k:94
uh s	t:1020 i:341 ee:229 uh:228 k:189 l:171 e:147 ay:134 p:78 aa:62 o:54 er:48
ay t	i:931 s:595 er:404 uh:362 r:77 ee:64 l:62 m:51 o:41 h:40 n:34 f:26
n i	k:716 ng:582 s:387 t:214 sh:163 g:124 n:110 r:93 m:85 z:79 f:78 l:54
uh m	p:373 uh:301 e:298 z:268 b:254 i:244 ay:203 ee:188 aa:158 a:131 er:126 o:89
b uh	l:1415 n:312 t:233 s:160 k:142 d:68 z:53 g:48 f:42 m:42 b:39 ng:37
r ay	t:526 n:399 d:298 z:226 s:210 k:185 v:148 l:146 b:135 m:112 uh:104 sh:95
l ay	t:550 n:484 z:454 k:176 s:159 d:153 sh:149 m:138 uh:91 b:89 v:82 f:58
m i	n:478 s:469 l:327 k:265 ng:261 t:241 d:138 r:137 sh:98 th:61 j:45 ch:42
p r	ee:409 uh:377 i:362 e:340 o:294 aa:257 ay:244 oo:108 a:80 or:53 ow:34
r a	n:719 k:314 m:275 f:234 t:177 ng:146 d:144 s:142 p:107 v:91 g:87 b:72
r e	s:474 n:400 k:288 d:287 l:176 t:173 z:141 m:134 sh:114 g:110 p:95 v:90
k r	i:439 a:301 uh:285 ay:281 ee:254 o:215 e:172 oo:167 aa:161 or:104 ow:60 oy:20
b r	i:363 ay:343 uh:309 a:298 oo:244 e:189 ee:177 aa:170 o:144 or:93 ow:72 oy:22
i l	uh:479 i:471 ee:269 d:163 t:143 e:137 er:137 o:128 b:122 k:121 z:117 y:116
uh k	uh:331 ay:283 aa:230 s:222 l:188 t:163 r:161 er:137 w:130 a:115 or:114 o:111
e k	s:734 t:659 uh:173 sh:144 er:117 l:63 n:58 w:52 o:51 ee:47 y:35 r:27
i t	s:527 ee:355 uh:293 i:279 er:214 r:121 ay:95 e:66 m:53 l:46 aa:40 z:38
g r	a:480 uh:300 ay:280 ee:254 i:193 e:190 o:152 oo:110 aa:93 ow:54 or:18
k l	ee:387 i:321 ay:230 uh:215 a:199 e:188 er:113 o:108 aa:104 oo:93 or:92 ow:43
t er	z:780 n:220 m:199 d:129 uh:126 i:122 b:118 ee:93 s:87 l:75 ay:71 k:67
m aa	r:955 n:441 l:133 t:128 s:118 d:94 k:55 g:34 m:32 ng:29 z:23 j:19
e s	t:873 uh:283 k:205 i:180 er:84 ee:78 l:77 p:77 n:45 m:41 ch:38 e:31
b er	g:761 n:266 t:254 d:136 k:105 l:90 z:83 s:74 ay:53 o:53 m:50 uh:47
o l	d:486 uh:274 ee:233 t:212 i:161 z:143 s:111 er:80 o:73 m:72 e:63 f:60
y oo	r:319 n:263 z:259 t:253 l:191 m:164 s:123 k:99 b:83 d:76 uh:72 g:55
i ng	k:455 z:438 er:295 g:276 t:164 l:117 h:57 uh:38 s:30 d:20 i:18 m:18
l a	n:581 s:248 k:216 m:186 t:142 ng:135 p:85 d:81 b:77 sh:71 g:55 f:45
r o	z:311 s:229 l:224 d:184 m:181 n:156 t:147 k:146 b:113 v:81 g:74 p:69
aa l	uh:570 ee:283 i:268 d:116 v:112 er:107 m:104 o:91 aa:83 e:72 t:49 z:49
uh b	uh:744 l:232 i:163 s:121 r:116 e:83 ay:79 aa:74 ee:69 a:68 z:67 or:65
ee uh	n:790 s:262 l:248 m:176 z:127 t:111 b:33 p:27 d:24 f:15 g:15 k:12
t e	l:370 k:338 n:316 r:280 s:136 m:131 d:65 t:61 p:58 f:51 g:17 v:16
g uh	l:641 n:596 s:155 t:121 m:96 z:62 v:52 d:40 b:28 f:22 k:11 p:11
s p	e:325 ay:175 r:175 er:169 aa:164 i:164 ee:147 l:129 a:113 uh:84 or:80 o:77
a k	s:549 t:386 uh:254 er:115 i:112 l:64 sh:63 y:60 r:56 w:53 ee:49 m:39
s e	n:380 l:327 k:185 t:164 m:152 p:140 s:131 r:129 v:54 sh:36 g:35 f:31
ay n	z:325 d:273 er:214 ee:187 uh:182 i:143 j:118 t:106 o:55 s:54 m:51 b:50
w i	n:388 t:290 l:281 s:177 k:152 ng:85 g:78 ch:66 d:64 m:57 p:53 sh:52
k a	n:372 l:255 s:245 m:219 t:166 p:162 r:94 d:54 b:45 sh:44 th:44 f:39
r aa	n:354 k:242 m:188 s:182 d:131 b:128 p:118 t:99 l:82 g:80 z:61 v:56
m e	n:470 r:298 l:232 t:177 d:157 s:98 m:74 k:67 g:46 z:34 th:25 sh:16
l e	k:368 n:262 s:203 t:175 r:174 m:117 v:116 g:64 j:58 f:55 p:48 d:42
l ee	n:321 uh:279 z:265 t:136 v:117 o:113 s:103 d:82 aa:69 b:66 m:66 k:60
ee n	uh:433 ee:363 o:351 z:180 i:71 er:66 y:51 aa:41 d:36 t:30 e:28 s:24
m a	n:483 k:260 s:204 t:186 l:140 g:111 d:75 j:47 th:43 ch:37 ng:37 r:31
k i	n:525 ng:393 l:172 t:155 s:117 m:85 d:47 r:41 p:35 k:31 b:25 sh:23
t ay	n:389 t:310 sh:158 l:154 m:115 k:108 z:104 p:86 r:55 s:53 b:49 d:37
k s	t:350 uh:318 p:227 i:209 ee:129 e:102 k:72 ay:61 er:43 l:42 m:30 b:28
a l	uh:519 i:239 ee:142 k:117 b:97 y:88 f:78 v:74 m:54 er:52 p:52 s:50
i m	p:444 uh:261 b:159 i:131 er:112 aa:83 ee:61 e:58 o:58 a:57 z:56 y:54
v i	l:272 n:247 ng:154 t:143 ch:133 k:132 s:129 r:69 d:61 zh:53 j:43 z:42
p uh	l:526 n:252 t:191 s:113 z:78 b:69 d:56 k:52 th:38 p:33 g:32 ng:27
a s	t:505 uh:260 k:182 i:161 p:77 ee:64 er:49 n:34 b:32 l:31 m:30 o:22
b e	r:401 l:334 n:236 k:164 t:78 d:74 s:47 v:31 th:22 g:20 ng:18 z:18
b aa	r:547 k:209 l:133 n:130 t:88 m:73 d:64 s:52 b:48 g:29 z:27 ch:17
a m	p:342 b:259 uh:253 er:148 i:107 z:81 s:48 ee:40 l:35 r:31 f:30 d:28
l aa	k:228 r:228 n:186 t:173 m:108 s:90 j:79 b:70 g:69 v:61 p:53 d:40
p l	ay:330 uh:231 a:178 i:150 e:102 ee:102 oy:63 aa:51 er:44 o:41 or:40 oo:38
e t	uh:258 s:249 i:221 r:185 ee:153 er:116 l:50 o:43 z:33 n:24 k:15 w:15
d e	l:361 n:225 m:117 s:114 k:107 r:107 t:81 v:64 z:52 b:46 d:43 f:42
m ay	k:273 n:240 t:151 er:131 l:122 z:104 r:67 d:64 s:62 sh:62 uh:34 g:20
p e	n:301 r:266 k:182 t:171 l:164 d:61 s:47 p:38 sh:32 ch:21 g:15 z:12
b i	l:340 n:170 t:140 k:133 s:103 g:78 r:77 ng:71 d:65 sh:51 z:48 f:28
z uh	n:402 m:317 l:275 z:85 b:60 t:53 k:39 d:20 g:15 v:13 f:11 s:11
f i	l:279 n:214 k:182 sh:143 ng:118 t:110 r:62 s:62 g:43 d:34 z:33 b:17
b l	ee:204 i:200 a:181 uh:146 ay:127 aa:110 oo:103 er:74 o:67 e:53 or:14 ow:14
f r	ee:246 a:200 e:171 ay:164 i:149 uh:147 aa:75 or:40 o:37 oo:31 ow:11 oy:8
t ee	n:425 z:352 l:71 t:63 m:57 v:51 uh:50 er:45 s:45 k:42 g:36 d:33
d r	uh:247 i:215 ee:152 ay:149 e:107 o:103 aa:85 a:81 or:55 oo:53 ow:11 oy:9
k or	r:776 f:169 l:98 s:50 z:33 k:22 n:22 t:21 ng:20 p:18 sh:14 m:13
r t	uh:243 i:221 s:200 ee:147 er:110 e:64 n:57 l:46 o:45 ay:43 r:40 m:39
k w	i:318 e:157 aa:156 uh:155 ay:154 ee:121 or:78 er:51 o:31 oo:19 a:13
p aa	r:360 l:215 n:194 s:89 p:88 t:86 z:52 m:48 d:35 k:35 v:30 g:15
m p	l:236 uh:188 er:124 r:117 t:107 s:87 ee:82 o:64 e:61 i:60 a:54 y:53
k t	i:403 er:247 uh:194 s:148 r:88 ay:49 ee:21 o:18 aa:17 l:17 e:15 or:13
p i	ng:251 k:204 n:183 t:153 r:149 l:104 s:62 d:31 g:31 ch:29 j:16 sh:15
aa k	s:373 uh:156 ee:102 er:97 t:87 aa:74 i:68 o:66 y:60 l:49 r:46 w:39
ay sh	uh:1189 ee:17
t aa	r:326 n:205 k:164 l:137 m:114 p:82 t:32 v:30 f:29 g:29 s:25 d:23
ay d	uh:269 z:174 er:163 ee:162 i:153 r:89 o:53 e:35 l:34 aa:24 ay:19 m:17
h aa	r:610 l:139 n:65 s:58 k:57 m:48 p:43 b:37 t:37 g:36 f:33 j:19
aa m	uh:330 p:209 b:130 i:104 aa:94 ee:68 z:55 er:50 e:44 o:41 s:30 or:23
k o	l:285 z:118 s:112 n:102 m:98 p:98 t:96 k:64 b:59 v:57 d:50 h:30
i r	ee:252 uh:195 i:135 o:116 z:107 e:77 aa:67 d:65 ay:51 s:43 a:31 m:26
s ee	z:217 n:138 d:130 uh:107 l:92 o:85 m:83 g:72 k:69 t:60 v:57 b:55
n ee	uh:371 z:208 l:82 k:78 n:66 s:66 d:60 o:49 t:49 m:46 er:44 a:37
e m	uh:240 p:234 b:201 i:143 er:111 ee:68 ay:40 l:27 f:25 s:23 k:21 e:20
o n	ee:445 uh:176 z:131 t:67 i:57 er:51 aa:47 d:44 e:37 o:37 y:27 ay:23
v uh	n:399 l:350 t:111 s:96 k:57 d:33 b:30 z:19 g:16 h:10 ch:9 j:8
w ay	t:226 n:196 l:146 z:138 d:91 s:73 k:56 r:49 v:48 er:46 uh:35 g:27
w e	l:334 s:233 r:182 n:141 t:64 b:42 d:37 dh:36 k:21 m:12 ng:12 p:12
ay z	i:369 d:277 er:184 uh:158 ee:75 m:20 aa:14 l:14 o:9
a t	uh:318 i:223 s:156 er:153 l:65 r:63 ee:36 k:29 n:23 m:18 w:18 o:14
d ee	uh:260 n:168 z:157 o:93 k:73 m:63 l:61 t:59 d:56 r:43 ay:42 v:41
b a	k:262 ng:166 n:161 l:108 t:98 s:94 r:64 g:55 b:36 d:27 sh:22 m:21
z i	z:342 ng:325 k:77 n:62 l:52 s:51 sh:51 t:47 g:34 r:28 p:22 d:21
p er	s:216 z:163 ay:114 t:99 m:83 f:76 uh:72 d:71 i:60 k:58 p:50 v:44
s l	ee:287 er:165 i:133 ay:132 uh:93 a:86 o:78 aa:62 e:26 or:23 oo:18
s ay	d:254 t:145 z:126 k:117 l:109 n:97 b:54 f:52 uh:40 v:39 m:32 p:27
r oo	k:131 m:127 t:126 d:116 p:99 s:96 z:81 b:75 n:73 l:69 f:49 v:43
uh f	ay:293 i:132 uh:119 ee:100 er:96 e:84 s:50 r:45 or:44 aa:39 o:37 t:35
ay l	z:216 uh:181 ee:149 d:131 er:127 i:105 ay:34 aa:30 y:28 m:25 o:23 s:20
f uh	l:408 n:266 s:130 k:77 t:43 m:36 ng:35 d:31 b:16 z:15 j:10
h a	n:367 m:211 l:101 s:57 ng:49 b:46 k:45 g:43 f:41 r:35 t:35 p:34
m b	er:220 uh:196 r:128 l:97 ee:79 o:73 aa:65 e:53 a:47 i:32 ay:29 or:21
n ay	t:367 z:183 sh:119 d:88 l:49 b:42 m:36 n:35 s:35 v:35 uh:26 k:25
uh d	uh:141 ay:132 ee:121 i:119 e:113 aa:78 or:69 z:58 l:56 oo:56 er:47 r:47
ng k	uh:263 s:140 er:132 i:87 o:74 ee:72 t:63 l:58 sh:48 w:38 r:32 aa:29
ee t	uh:205 er:157 s:155 o:128 r:85 i:77 ee:58 ay:46 aa:43 e:41 a:17 l:16
n e	t:220 l:183 r:172 k:142 s:104 g:35 n:31 v:30 z:30 d:26 f:25 p:18
uh p	r:161 l:121 ee:97 s:90 aa:78 i:78 uh:77 t:73 ay:69 o:63 e:53 or:51
aa t	s:230 uh:224 o:106 ee:96 i:93 er:70 aa:61 l:35 r:33 e:23 m:20 k:19
j uh	n:370 s:129 l:125 z:77 t:54 m:51 ng:47 b:43 k:42 j:26 g:24 d:17
t a	n:290 k:130 l:113 m:78 t:76 ng:66 g:51 b:49 f:43 s:42 ch:36 p:27
aa s	t:244 uh:174 k:125 ee:103 aa:73 i:67 e:46 p:41 o:39 l:35 b:27 ch:22
ay k	er:220 uh:199 s:106 i:98 r:92 o:66 l:60 ee:46 aa:40 or:28 m:21 ay:20
h e	d:198 l:196 r:166 n:156 m:78 k:52 s:32 f:29 t:28 v:21 g:19 b:17
f l	a:160 ay:144 i:115 e:92 or:86 uh:84 oo:73 er:61 ee:59 o:38 ow:37 aa:35
v er	s:185 t:148 z:133 d:80 i:73 b:69 n:69 l:57 ee:47 uh:40 h:39 m:38
d ay	t:156 n:155 l:134 uh:105 z:94 v:89 m:48 k:44 s:40 sh:37 r:33 a:29
k ay	t:261 sh:146 n:98 d:80 l:78 s:65 p:58 z:52 b:35 m:31 k:30 v:29
ee l	d:291 ee:161 uh:101 i:88 z:59 ay:56 aa:50 er:45 o:31 a:29 e:25 m:25
n aa	n:195 r:173 t:107 l:81 k:79 m:71 v:59 d:46 s:45 b:37 g:32 p:31
e d	uh:212 i:194 er:95 ee:71 l:71 z:71 r:66 m:42 w:39 b:28 f:27 k:27
l t	uh:199 s:181 er:146 ee:124 i:104 r:55 ay:32 aa:25 m:22 o:21 e:19 z:13
ay s	i:191 uh:190 t:188 ee:92 er:61 l:48 m:39 k:35 b:30 e:30 w:18 aa:16
k er	z:270 t:104 d:91 uh:77 i:72 n:59 ee:56 k:55 s:53 e:35 b:33 m:33
uh g	r:239 ay:139 uh:124 i:72 aa:67 l:60 n:42 o:41 a:37 z:36 ee:35 er:34
er s	uh:275 t:261 i:78 e:74 ee:59 ay:40 k:34 p:25 aa:21 er:20 l:20 oo:16
i v	i:158 uh:128 er:126 l:90 z:71 ay:69 e:66 ee:62 a:53 n:38 o:33 aa:26
d er	z:281 m:97 i:73 s:70 d:68 uh:63 l:52 k:47 e:45 b:43 n:33 w:30
i g	z:185 uh:173 n:122 er:108 l:57 r:51 i:42 m:42 ay:36 aa:34 y:27 o:24
or l	t:205 d:158 z:110 uh:93 i:71 ee:52 s:52 er:46 m:35 b:31 k:28 p:19
i p	s:175 uh:118 i:89 t:87 l:76 er:63 ee:62 r:55 aa:47 e:44 o:34 or:32
l d	er:188 uh:159 z:123 i:118 r:69 ee:60 o:45 s:27 m:25 w:22 ay:18 f:18
y uh	l:314 n:261 m:52 t:49 ng:37 s:30 z:28 p:24 b:23 w:22 k:20 v:9
f e	r:210 l:202 n:118 k:99 d:85 s:62 t:41 dh:17 sh:16 m:14
g l	ee:186 i:124 a:121 er:84 uh:78 ay:71 e:47 o:44 aa:43 or:38 oo:24
j i	z:201 ng:128 n:110 k:108 s:99 t:69 r:39 m:27 l:26 b:22 p:15 g:13
p a	n:209 s:136 k:124 t:97 l:81 d:40 th:39 ch:30 ng:29 m:22 p:22 sh:18
a ng	k:530 g:201 er:22 z:22 l:15 uh:15 s:13 h:10 b:9 d:8
uh v	i:244 er:143 ay:115 e:75 uh:73 aa:53 a:36 ee:36 o:20 r:17 or:15 oy:15
s aa	r:177 n:176 l:170 k:68 m:44 d:43 b:36 v:34 t:28 f:26 p:19 g:11
d aa	n:178 r:155 l:135 k:114 m:82 t:31 b:26 g:25 s:25 p:23 v:17 d:16
i sh	uh:420 i:103 t:57 er:47 ee:42 m:37 n:28 l:22 e:21 oo:19 b:15 k:9
w or	r:337 l:228 t:114 k:62 n:25 s:18 ng:15 sh:11 d:10
r d	uh:178 ee:145 i:115 z:115 o:79 er:51 e:27 ay:26 aa:21 n:20 r:20 w:20
t s	k:168 uh:141 ee:96 oo:73 m:72 t:58 i:48 aa:35 er:35 o:31 b:30 ay:27
g aa	r:365 l:105 n:94 d:63 t:50 s:34 b:26 m:20 g:18 ch:16 w:12 k:10
h ay	d:144 m:123 n:118 l:73 p:62 t:53 s:51 z:44 v:42 k:35 g:34 b:29
f er	d:263 m:85 t:82 z:74 n:57 uh:47 s:43 e:37 ee:34 i:32 g:31 ay:20
o v	er:446 uh:83 ee:70 i:52 aa:50 e:31 o:22 a:18 ay:18 z:10
l o	z:135 n:123 d:114 k:76 s:69 m:59 v:47 p:44 b:43 t:43 i:25 g:21
er m	uh:429 i:87 ay:63 a:38 ee:38 e:32 aa:28 z:24 er:20 o:16 y:13 d:8
l oo	k:106 s:102 m:98 t:94 d:69 r:68 b:60 p:44 i:40 sh:39 n:37 z:36
i z	uh:384 ay:70 i:65 er:53 ee:42 n:39 e:35 m:30 l:23 o:18 aa:16 r:15
k e	r:257 n:136 l:135 m:76 t:57 s:47 p:21 ch:20 j:18 v:13 z:9
l or	r:331 ng:86 f:70 n:67 s:56 g:45 d:38 z:30 t:22 k:18 m:13 l:11
i f	er:115 t:113 i:91 ay:82 uh:78 e:56 r:55 ee:49 or:42 s:42 l:39 y:24
er i	ng:370 s:79 k:71 j:62 n:45 z:36 l:31 sh:30 t:26 r:14 f:10 m:9
f or	r:657 l:74 n:18 k:15 s:15
oo r	uh:222 ee:123 i:95 o:94 aa:49 z:42 d:38 e:28 ay:25 n:23 er:19 oo:19
f ay	n:146 d:115 er:81 t:73 z:67 l:63 i:52 r:52 s:42 v:39 b:30 g:16
o s	t:235 uh:117 ee:106 k:65 i:62 e:43 o:35 p:25 aa:24 ay:24 er:22 m:18
t o	n:260 l:126 z:80 m:66 s:47 k:44 t:39 p:27 b:25 d:25 v:22 f:15
ng g	uh:276 er:94 l:82 o:79 r:53 i:51 w:41 e:38 aa:26 ee:15 y:10 or:9
oo l	uh:158 i:128 ee:126 er:61 f:60 z:59 o:40 d:34 m:31 ay:26 t:26 e:25
ee s	t:197 uh:119 ee:112 i:84 k:53 e:49 er:45 p:33 ay:30 or:18 o:17 aa:15
v e	n:233 r:155 l:141 s:125 t:58 k:25 z:12 j:11 d:10
ee d	uh:165 ee:161 i:130 er:92 z:56 r:38 o:35 l:24 e:20 m:18 ay:17 oo:12
o t	uh:181 o:112 s:87 i:82 e:70 ee:68 er:53 ay:31 r:29 aa:24 oo:10 a:9
ee k	uh:146 o:123 w:79 aa:74 s:55 a:51 er:51 l:44 r:41 i:37 or:25 ee:24
t l	ee:323 i:92 er:91 uh:86 ay:52 a:48 e:18 o:17 or:11 aa:10
j e	n:273 k:99 r:85 s:83 l:60 t:49 f:35 m:28 ng:17 p:10 d:8
i d	uh:177 ee:114 i:85 l:64 z:52 ay:49 e:48 er:42 o:34 n:30 w:25 aa:24
er uh	l:189 n:174 s:104 t:92 b:68 p:30 z:19 j:14 m:14 g:13 k:13 d:9
s m	uh:338 i:109 a:47 ay:45 aa:43 o:39 e:38 or:29 er:21 oo:20 ee:9
s a	n:223 l:94 m:86 k:85 t:65 s:38 ng:35 d:33 v:24 f:22 b:17 p:15
m o	n:173 t:121 l:89 s:59 b:57 z:54 d:50 k:42 sh:34 g:20 h:17 m:17
p ee	t:105 n:97 uh:96 k:78 d:72 s:65 l:54 z:52 ch:34 r:32 p:26 e:22
er t	uh:180 s:131 i:99 ee:97 ay:79 er:30 o:24 r:22 e:21 l:20 oo:16 a:11
oo t	s:147 uh:132 i:119 er:92 ee:54 r:48 o:43 ay:25 aa:23 l:19 e:15 oo:13
o m	uh:184 aa:79 o:62 i:60 ee:59 e:56 ay:53 a:45 er:45 z:34 b:20 s:13
b ay	t:116 s:106 n:100 l:86 o:56 k:48 d:40 b:38 uh:32 z:30 r:29 g:25
sh i	p:184 ng:110 z:98 n:67 r:55 m:42 s:36 l:32 f:29 k:17 t:17 v:12
g i	l:194 n:159 ng:76 v:49 t:48 s:47 r:34 b:21 m:21 f:18 d:16 g:15
v a	n:387 l:155 k:56 s:36 ng:15 p:14 m:13 d:9 t:9
r m	uh:311 i:71 z:51 er:46 ee:37 aa:35 ay:31 d:26 o:25 e:23 a:22 y:15
l m	uh:375 ay:49 er:47 aa:39 z:34 i:33 e:31 or:21 o:19 a:18 ee:14 s:10
w aa	n:160 l:106 r:72 t:68 d:50 sh:48 ch:42 m:39 k:30 s:28 z:25 ng:21
a d	uh:193 i:77 v:77 l:53 m:50 z:45 ee:43 er:36 o:31 r:30 s:28 n:25
r k	uh:178 s:94 i:82 ee:72 o:61 l:39 er:38 t:27 r:26 ay:25 w:24 a:19
h uh	n:146 m:121 l:114 s:53 f:41 b:37 k:33 d:32 g:29 t:29 v:24 ng:23
n b	er:286 aa:67 e:66 r:46 ow:40 ee:39 uh:33 a:23 or:23 o:22 l:19 ay:17
m er	z:128 ee:79 s:67 ay:63 uh:60 i:58 k:47 t:45 j:35 d:33 l:32 a:31
aa p	uh:116 er:112 s:80 t:74 i:68 ee:64 l:35 aa:30 o:29 y:28 k:20 r:18
ee m	uh:209 i:63 er:60 ee:52 o:50 z:49 ay:46 aa:44 e:30 or:26 a:23 d:17
m ee	n:140 t:105 d:92 uh:81 z:73 l:61 k:34 o:18 g:17 er:16 s:16 aa:14
p ay	n:103 s:89 t:81 l:72 p:66 r:47 d:44 er:39 sh:36 k:31 z:30 uh:24
aa d	uh:183 ee:101 o:77 i:66 er:49 r:44 z:38 aa:35 l:27 e:14 m:12 or:12
m or	r:534 l:67 s:27 n:21 j:8
t or	r:484 l:50 f:42 k:34 n:27 s:10 p:8
h o	l:349 m:115 n:27 s:25 z:23 g:20 p:20 f:18 v:17 k:16 t:13 b:10
k y	oo:429 uh:171 er:28 aa:13 o:8
o k	uh:148 aa:71 ee:67 er:63 s:60 o:53 ay:50 i:36 l:31 a:28 e:23 oo:19
l er	z:291 ee:62 d:52 ay:51 t:39 i:29 k:29 uh:27 j:23 b:15 l:14 n:14
er n	uh:146 i:83 z:76 ee:69 er:64 ay:43 a:36 aa:33 d:26 e:26 s:21 o:18
ay m	uh:190 er:109 z:101 ee:57 i:40 d:30 b:26 ay:19 l:19 aa:17 o:17 a:15
n er	z:252 d:60 uh:55 ay:53 ee:51 t:36 s:33 i:25 j:22 a:18 v:18 e:15
ow n	d:211 t:185 s:102 z:54 i:23 ee:19 er:18 l:10 p:8 uh:8
w er	k:160 d:119 th:117 l:73 t:39 m:35 dh:28 s:25 n:20 sh:12 ee:9
n a	n:121 k:82 sh:75 p:65 l:63 ch:48 s:41 t:37 m:35 v:25 d:20 b:19
n o	z:107 t:80 v:78 l:77 s:65 b:61 m:47 n:32 k:28 d:24 g:18 f:12
ay v	er:163 uh:129 i:71 z:68 ee:67 d:31 l:23 e:22 y:20 aa:14 ay:9 o:9
er ay	t:248 z:137 sh:130 n:49 d:23 uh:14 v:12 s:10
s er	v:109 z:104 t:77 k:68 ee:57 j:36 p:36 uh:30 ay:29 f:28 n:26 b:23
h i	l:184 n:83 r:72 s:58 p:46 k:37 b:32 m:30 t:25 g:20 ch:19 ng:15
l s	uh:213 t:171 k:68 i:43 ee:42 er:22 ay:19 b:11 e:10 m:10 p:10
a p	uh:152 s:93 t:92 i:74 er:60 l:47 r:31 ee:28 ch:12 sh:10 n:9 k:8
b o	l:190 n:114 t:73 d:54 z:44 s:37 k:29 g:18 m:18 v:14 b:12 er:12
oo n	uh:144 i:116 z:65 ee:62 d:49 er:32 y:31 aa:27 o:26 ay:22 e:22 t:18
n f	er:87 e:86 or:70 l:68 r:57 uh:54 i:52 ee:42 ay:34 o:23 aa:20 y:20
v ay	t:104 z:78 l:77 d:62 n:59 uh:40 sh:38 r:37 v:30 s:29 k:26 g:20
ay uh	n:205 l:95 t:91 s:66 b:37 m:34 k:23 g:15 d:13 z:12 w:8
b ee	uh:119 z:79 n:59 t:58 l:57 k:43 s:38 aa:36 d:31 ch:30 b:26 m:23
k ee	z:128 n:91 uh:63 p:53 t:50 l:46 o:43 m:35 aa:33 s:22 g:18 f:17
ee v	uh:109 er:94 ee:74 i:62 a:47 z:47 ay:40 e:35 o:25 aa:24 d:23 y:14
p o	l:180 s:109 z:107 n:54 k:45 t:28 p:20 d:14 uh:10 ch:9 v:9 m:8
b oo	k:133 l:104 sh:63 r:62 t:60 d:32 n:30 m:28 s:25 ch:21 z:14 g:11
n k	uh:94 aa:74 l:66 or:66 r:64 o:53 w:44 a:40 er:32 ay:21 ee:17 i:12
ay b	uh:170 er:157 ee:67 r:63 i:27 l:19 z:19 a:13 o:13 or:13 d:12 aa:8
ch i	z:148 ng:103 n:71 k:69 l:64 p:31 t:31 r:25 s:22 m:17
oo m	uh:177 i:74 er:66 z:60 ee:41 aa:35 e:32 ay:29 a:21 d:21 o:12 y:12
b or	r:355 l:125 s:23 t:16 n:15 m:13 k:12 f:10 d:9
er l	ee:183 i:124 uh:69 ay:47 d:44 a:33 z:23 aa:16 o:16 e:11 oo:10
n j	er:167 uh:160 i:59 ee:58 e:55 d:25 oy:19 oo:14 o:10 m:9
e v	uh:185 er:157 i:99 r:50 ee:37 l:16 aa:12 z:12
a g	uh:140 n:100 l:55 r:51 er:50 z:40 i:36 d:30 m:26 ee:21 s:9 w:9
r n	uh:119 i:79 ee:67 er:57 z:52 b:40 o:36 aa:27 e:27 d:22 ay:21 s:20
s w	i:113 e:87 aa:86 ay:74 ee:69 er:50 or:46 oo:21 a:10 o:8
o d	uh:126 ee:85 i:72 er:54 z:52 aa:45 e:39 ay:30 o:20 r:20 a:11 oo:9
r s	uh:121 t:106 i:81 k:57 ee:55 e:33 ay:31 er:20 m:17 o:16 l:13 f:9
r or	s:109 d:74 l:69 n:59 th:53 r:51 f:48 ng:35 b:15 t:15 z:15 m:13
v ee	n:119 uh:118 l:57 t:46 z:44 ay:36 e:28 o:27 d:22 aa:20 s:18 v:17
d l	ee:200 i:94 er:88 uh:69 ay:39 aa:22 a:13 o:13 or:13
o b	uh:135 ee:81 er:79 i:46 aa:41 r:40 o:31 ay:30 e:18 l:18 a:16 or:16
oo d	uh:139 z:84 i:81 ee:69 l:30 er:29 r:22 ay:21 o:19 s:19 w:19 e:13
oo s	uh:124 t:99 i:90 ee:54 o:40 k:33 e:25 er:23 aa:21 ay:15 m:10 f:8
i b	uh:134 er:73 i:50 ee:49 r:44 y:42 l:34 aa:30 e:26 o:20 oo:16 z:16
f ee	l:274 n:53 z:40 uh:34 d:32 t:25 or:19 b:14 g:13 m:13 r:8 s:8
uh j	ee:150 uh:124 i:122 e:60 d:21 er:17 ay:15 m:11 or:11
a b	uh:121 er:70 i:67 r:55 s:48 d:37 z:34 l:31 ee:30 k:14 n:13 y:10
f aa	r:193 n:100 l:70 s:53 k:35 dh:28 v:12 b:11 g:11 t:10
er k	uh:138 s:69 i:60 er:48 l:38 ee:32 o:27 y:27 h:25 or:22 aa:17 w:17
l b	er:181 r:61 ee:51 uh:43 aa:30 e:30 o:30 a:24 or:21 i:18 ay:15 ow:15
g a	l:121 n:102 s:73 r:47 m:46 t:29 ng:24 b:19 d:18 g:14 v:11 f:9
aa b	uh:85 ee:65 er:53 l:51 i:49 r:46 z:44 s:42 aa:31 e:20 o:16 oo:10
a f	t:172 i:74 r:53 uh:50 er:45 ee:35 s:35 l:18 n:13 ay:8 g:8
w ee	l:99 n:81 t:68 k:62 d:52 z:49 s:24 v:18 g:14 p:14 b:13 uh:11
e p	t:116 er:90 uh:77 s:48 i:42 r:36 l:33 sh:31 ee:20 y:10
oo k	s:107 uh:99 er:51 ee:39 aa:36 i:35 l:34 ay:29 o:26 t:16 m:15 a:14
t oo	r:81 n:67 d:65 t:64 l:46 m:41 p:29 b:27 k:22 s:20 z:20 sh:18
h or	r:243 f:87 l:82 k:46 g:12 n:10 t:10 s:9
g ay	t:173 l:54 n:54 d:39 sh:37 m:34 z:28 s:25 j:18 b:16 v:11 er:9
v aa	l:148 r:116 n:104 s:35 k:32 d:20 t:15 z:13 g:12
k oo	l:91 n:62 r:62 k:59 p:56 t:41 s:30 m:25 d:19 b:18 ch:16 sh:15
n z	uh:109 ee:82 i:68 o:46 b:38 er:31 l:29 m:22 d:19 aa:18 w:17 ay:14
p or	r:348 l:65 n:24 s:20 z:12 f:10 p:8
d a	n:145 m:79 l:51 k:37 t:34 d:28 sh:20 ng:19 g:18 p:18 f:16 v:16
er b	er:71 uh:71 i:55 ee:47 ay:43 a:35 or:34 e:31 o:29 aa:28 r:19 z:18
d or	r:314 g:33 l:30 n:28 f:27 t:20 s:16 ng:12
d o	n:114 l:96 z:62 m:58 s:41 b:25 v:24 t:22 d:15 uh:11 p:8
s n	er:87 uh:75 i:65 o:46 a:44 ay:40 e:35 ee:27 or:24 aa:18 oo:12
s oo	p:135 t:62 m:47 n:36 k:35 z:34 d:33 l:22 r:18 b:16 ee:14 s:14
l k	uh:136 o:61 aa:50 i:42 er:30 or:29 s:26 ee:24 a:22 w:17 ay:13 r:11
ch uh	n:139 l:91 z:53 m:36 s:32 k:25 t:20 b:19 w:18 p:10 v:9 g:8
r l	ee:115 uh:96 i:90 ay:36 o:35 or:19 z:18 s:15 a:10 aa:10 er:8 t:8
n or	r:298 f:64 t:33 l:24 g:18 s:12 n:9
ee aa	n:179 l:63 r:59 t:41 k:22 g:21 s:16 d:15 ng:12 m:11 v:9 p:8
aa g	uh:94 r:70 n:46 aa:37 l:37 o:37 i:31 w:23 er:21 d:20 z:19 ee:17
n oo	z:76 t:61 m:56 k:55 r:38 n:34 b:30 l:24 d:20 s:19 uh:19 v:19
g o	l:162 z:46 n:42 s:37 d:32 t:29 sh:25 i:17 v:16 m:15 b:14 er:11
s o	l:134 n:54 s:38 sh:34 t:31 d:28 z:28 b:26 m:21 k:18 f:16 v:16
er d	z:145 uh:61 i:52 ee:44 l:29 ay:25 er:23 aa:15 a:12 o:12 r:12 e:11
ch e	k:111 r:79 s:73 l:57 n:45 v:21 t:13 m:12 f:10 ch:9 z:9
i j	i:152 uh:128 d:36 e:36 er:36 ee:20 m:15 oo:10
r b	uh:108 er:68 i:61 e:41 o:32 aa:25 ee:22 or:19 ay:17 r:13 a:12 oo:9
er g	er:170 uh:71 z:45 r:35 i:22 l:20 aa:15 o:15 e:13 ee:10 s:9
n y	uh:206 oo:109 er:32 o:21 aa:19 e:17 or:11 ee:9
a r	uh:199 i:136 ee:53 o:35
e g	uh:96 r:48 er:46 l:41 z:40 y:36 n:31 i:26 m:22 ee:12 o:12 aa:10
g er	z:100 t:64 d:54 i:43 s:35 ee:30 l:27 n:17 aa:12 ay:12 b:12 g:12
f a	n:85 k:76 s:56 l:52 b:29 t:24 m:21 r:20 d:18 sh:14 g:12 ng:10
d oo	s:85 r:73 b:37 k:33 g:26 l:26 p:26 n:25 d:22 i:22 uh:21 m:20
or f	s:241 t:47 er:38 uh:36 i:22 m:18 l:11
uh z	i:70 ay:65 uh:53 e:50 ee:42 aa:30 o:27 er:22 oo:19 l:13 a:11 n:11
e f	s:79 er:76 uh:60 t:43 r:34 i:30 l:22 y:15 ee:14 aa:13 e:12 k:10
ay p	er:121 uh:60 s:46 i:33 ee:28 t:24 r:20 aa:17 o:17 l:14 e:12 a:11
w oo	d:269 l:77 m:28 p:21 n:8
l v	i:70 er:68 uh:67 ee:57 ay:33 aa:26 e:25 z:25 o:16 d:15
n ch	er:70 i:70 uh:60 ee:53 e:37 t:24 a:23 ay:20 oo:18 o:15 m:10
oo p	er:167 s:37 i:35 uh:35 ee:29 l:22 r:19 t:17 aa:12 o:11 ay:8 e:8
s or	r:157 l:72 f:49 ng:26 d:23 n:23 s:19 t:11 z:10 k:8
ee g	uh:139 er:58 o:41 l:34 r:28 aa:26 n:19 e:18 i:12 m:10 z:10
ee p	uh:70 er:63 ee:44 r:42 l:33 i:23 s:23 aa:20 ay:20 o:20 a:18 or:18
o z	uh:126 i:55 ee:41 er:35 d:33 aa:21 ay:14 o:14 m:13 a:12 b:12 e:12
n v	e:95 i:83 ay:54 er:47 uh:38 ee:25 o:13 a:10 aa:10 oy:10
p t	i:138 uh:96 er:43 s:34 ee:17 ay:16 o:15 aa:9 e:9 r:8
w uh	n:120 l:115 t:58 z:32 s:31 d:11 b:8 k:8
l y	uh:214 oo:90 er:38 aa:17 a:9 e:9
aa v	uh:77 ee:66 e:41 aa:39 i:32 o:24 ay:19 r:18 s:17 z:17 er:13 l:13
ay r	uh:95 i:41 o:40 z:36 ee:34 aa:30 ay:29 e:25 m:15 oo:11 d:9 l:8
g n	uh:111 o:53 ay:49 er:38 aa:28 ee:28 i:26 e:25 or:13
h w	i:141 ay:85 e:42 ee:37 aa:21 a:12 uh:12 er:10 oo:10
or s	t:146 k:49 uh:42 i:34 ee:23 er:20 p:17 l:13 m:13 b:12
ee z	uh:117 ee:47 i:44 er:39 e:26 l:23 aa:13 ay:12 o:12 d:11 oo:11 m:10
k sh	uh:309 i:15 er:12 ay:8 e:8 oo:8
n h	aa:60 uh:52 ay:45 a:43 o:36 ow:33 e:25 i:23 or:19 oo:14 er:10
uh ng	k:267 g:55 z:18 er:12 s:8
ee b	uh:86 er:58 ee:44 o:26 e:25 r:24 aa:23 i:20 or:16 oo:13 ay:12 a:11
ow t	s:109 l:44 i:31 uh:26 f:24 p:23 er:22 g:20 r:17 d:14 m:14 w:14
j ee	n:82 uh:61 o:57 z:54 aa:28 l:22 m:16 t:13 ay:8 e:8 s:8
ch er	z:116 d:48 uh:48 i:31 n:22 ch:20 ee:17 ay:15 er:14 t:14 aa:10
t m	uh:249 ay:27 aa:19 a:16 e:13 or:11 er:10 i:10
oo b	uh:90 er:68 i:48 ee:41 r:32 e:26 aa:16 l:12 o:12 z:8
z ee	n:61 uh:58 z:47 o:44 er:32 l:26 a:18 m:18 aa:16 b:11 t:11 g:10
sh e	l:88 r:82 f:38 p:24 d:23 v:23 k:22 t:22 n:16 ng:13
ay g	uh:137 er:96 r:39 l:19 o:19 ee:14 aa:12 i:10
oo z	i:84 uh:65 ee:48 d:43 er:33 m:15 l:10 o:10 p:10 w:9 aa:8 e:8
a sh	uh:118 i:59 n:31 t:28 er:24 ee:17 b:16 l:16 k:13 m:11 w:8
h ow	s:209 z:42 er:25 t:15 n:12 uh:12 l:10 d:8 k:8
a v	uh:131 i:94 er:38 l:31 r:24 ee:15
er ee	z:114 n:88 uh:37 t:16 d:15 k:13 er:12 l:11 m:11 s:11
m y	oo:254 uh:74
n l	ee:133 uh:52 ay:47 i:34 o:18 aa:15 a:12 or:9 oo:8
n sh	uh:176 i:46 oo:33 ee:23 ay:19 or:13 aa:8 er:8
f s	k:278 t:21 uh:18 ee:8
h er	n:51 s:51 t:46 m:31 b:30 d:29 sh:28 aa:16 l:16 z:15 a:12
g e	r:94 t:57 l:56 s:46 n:27 m:12 z:12 b:9 d:8
t w	i:74 ay:55 er:48 e:42 aa:39 ee:22 oo:22 or:18
l f	er:60 uh:49 ee:46 i:43 r:34 s:24 aa:19 ay:16 o:12 a:8 l:8
ee o	l:68 z:56 n:54 t:36 s:32 p:22 k:18 d:14 m:9 v:8
o p	uh:65 ee:53 r:34 s:27 a:22 l:21 ay:19 aa:18 er:15 i:15 e:14 k:13
ee e	r:72 n:66 l:53 s:36 m:33 t:31 f:14 k:9
er v	i:76 ay:75 uh:59 ee:22 er:20 d:17 z:15 a:11 y:11 e:8
r g	uh:108 er:42 i:42 o:41 aa:18 r:16 y:14 a:12 ay:11 e:8
s b	er:113 ee:48 aa:29 e:27 r:22 a:20 i:17 uh:17 or:16
k ow	n:179 t:38 s:24 l:18 uh:15 er:12 ee:10 r:10
ee ay	t:220 sh:65 ch:10 z:10
r ow	n:120 t:54 s:41 d:36 z:23 l:20 ch:9
i ch	uh:59 i:56 er:52 oo:28 e:25 ee:23 t:18 f:12 m:11 l:10 b:8
t n	er:130 uh:80 i:48 ee:35 aa:8
or t	er:129 uh:74 o:33 s:18 ee:17 i:17 aa:12
sh ee	ay:78 l:36 n:31 t:26 uh:26 m:23 k:15 o:14 z:14 d:12 b:11 e:10
th uh	n:108 m:61 l:60 s:25 d:18 k:11 g:8
k m	uh:166 a:29 ay:28 i:19 er:14 aa:13 or:12 e:9
uh w	ay:112 i:50 uh:48 aa:36 e:28 or:16
d m	uh:150 i:48 ay:27 aa:24 a:15 er:13 or:11
f o	l:77 n:70 t:47 k:42 m:20 b:17 g:15
ee a	n:111 k:106 t:27 l:23 d:12 s:8
y aa	r:100 n:51 m:45 k:24 s:24 t:13 z:10 l:9 ng:8
sh aa	r:122 p:44 t:35 k:23 n:21 l:12 d:10 b:8 m:8
er e	k:69 n:64 t:64 s:30 l:29 r:25
m oo	r:77 n:45 v:37 s:26 th:23 d:20 l:15 t:13 z:13 dh:10
b y	oo:223 uh:45 or:9
k n	er:53 i:53 ee:47 uh:44 aa:19 e:17 ay:14 a:10 o:9 oo:9
or n	t:73 d:44 z:41 ee:26 i:23 ch:22 uh:21 s:15 er:9
z e	l:71 n:65 m:40 k:38 r:22 t:22 s:16
n r	ee:66 i:58 o:36 ay:31 uh:30 a:20 e:13 aa:10 oo:8
uh h	a:66 aa:48 o:34 ee:31 ay:20 oo:20 e:12 or:12 uh:11 er:9 y:9
z ay	sh:89 n:42 d:31 l:24 t:16 uh:15 i:14 m:12 g:11 r:8 s:8
ee i	n:67 s:61 ng:58 t:19 m:17 th:12 z:12 g:9 sh:8
g oo	d:121 l:35 s:27 g:18 n:18 r:17 ch:14 t:13
z aa	r:71 n:61 l:44 k:25 m:19 d:13 t:10 v:10 p:9
d w	i:65 e:46 ay:44 er:33 or:31 aa:13 uh:11 oo:9 ee:8
e b	uh:60 er:53 r:32 i:24 ee:18 l:14 z:14 e:11 o:9 s:9 n:8 y:8
o g	uh:102 r:49 l:26 er:23 aa:19 ee:17 o:16 i:8
d s	t:117 uh:75 k:20 e:13 p:13 ay:12 l:8
j er	z:83 n:34 m:32 ay:18 ee:18 d:16 uh:14 i:12 aa:11 g:11 k:8
j oo	l:81 d:34 r:31 uh:25 n:23 v:12 s:11 b:10 k:9 i:8 p:8
oo uh	l:136 n:59 s:36 t:20
g or	r:167 l:34 n:19 f:15 s:14
er f	er:39 uh:39 i:35 or:33 ee:31 l:26 e:23 y:12 a:8
f t	er:64 s:61 i:44 ee:34 uh:29 w:12
aa z	uh:64 ee:37 o:27 n:23 aa:21 i:21 m:21 l:12 d:8 oo:8
ay er	z:128 d:26 n:20 i:14 b:12 l:12 ee:11 s:9 t:9
er aa	n:53 r:33 t:26 l:25 d:23 zh:16 f:13 s:13 g:10 m:10 v:10 k:9
z er	z:80 v:69 d:27 t:19 k:14 b:13 ee:10 e:9
z m	uh:122 i:38 a:18 o:16 aa:15 ay:14 er:10 e:8
j a	k:105 n:51 m:19 ng:15 s:13 f:10 g:10 z:9 b:8
ch ee	n:47 o:33 z:33 t:23 v:19 k:18 p:15 uh:15 aa:14 f:12 d:9
e z	uh:98 i:46 er:21 n:19 m:18 ee:15 b:11 l:9
ee r	z:39 ee:38 e:33 uh:31 i:23 o:22 ay:20 s:11 aa:10 d:10
f oo	l:74 t:57 d:27 r:23 j:19 k:15 s:11 n:9
e sh	uh:148 er:30 ee:14 i:13 l:13 k:8 n:8
sh a	d:39 n:37 m:34 k:28 ng:22 t:21 l:20 f:17 p:15
sh ay	n:46 k:33 d:32 m:25 r:19 p:16 v:16 er:14 l:14 z:10 b:8
ch a	n:62 t:39 k:29 m:26 p:25 l:18 d:11 f:10 s:10
p y	oo:165 uh:65
a ch	uh:57 er:55 i:38 ee:19 oo:19 t:19 r:14 m:8
v o	l:91 k:41 t:36 n:17 g:14 s:14 z:12
h ee	l:34 r:29 t:29 d:25 m:25 b:13 n:13 s:13 g:11 k:11 v:10 z:10
l ow	d:37 er:36 t:29 s:28 n:20 r:19 b:14 z:14 uh:10 k:8 m:8
aa f	uh:47 ee:39 s:36 i:33 er:25 t:18 aa:15 l:9
o sh	uh:93 ee:69 i:21 aa:10 ay:10 er:10 e:8
p oo	l:86 r:25 n:24 t:21 s:20 ch:13 sh:12 z:11 p:9
p s	uh:57 t:52 i:34 ee:27 k:19 ay:11 e:10 or:8
aa ng	g:110 k:106
b s	t:86 uh:47 k:29 i:19 e:16 ay:9 er:9
er p	r:66 uh:36 l:23 ay:17 i:17 ee:15 e:13 a:10 or:9 ow:9
g w	i:57 aa:53 ee:30 uh:24 ay:21 e:11 er:9 oo:9
ow s	k:68 i:48 t:25 er:23 uh:20 m:11 l:10 w:8
sh oo	r:56 l:36 t:35 m:24 uh:15 d:10 p:10 b:9 z:9 g:8
uh sh	uh:37 ee:33 i:30 oo:26 e:21 ay:17 er:17 aa:12 k:9 t:9
v l	ee:108 i:46 uh:25 a:12 er:12 aa:8
h oo	d:62 t:25 l:24 s:21 k:19 n:14 v:12 b:10 p:10 z:10
zh uh	n:186 w:10 z:10
n m	uh:121 ay:23 e:21 i:15 a:13 aa:12
n w	ay:35 i:34 e:30 er:29 or:26 oo:22 aa:16 uh:12
ee f	er:31 i:28 uh:23 e:20 l:18 s:18 ay:14 ee:12 or:12 r:10 o:9 oo:8
er j	uh:63 i:55 ee:32 e:16 er:16 d:12 ay:9
z b	er:76 ee:35 e:28 r:17 aa:14 uh:12 a:11 i:10
uh ch	ee:39 er:37 i:36 e:32 uh:25 oo:17 aa:8 or:8
f y	oo:199
s f	er:44 i:35 uh:35 ay:23 or:23 ee:17 e:13 a:8
t k	uh:46 i:31 o:23 or:22 ee:20 aa:15 a:12 l:11 r:10 ay:8
ay i	ng:147 k:18 s:11 n:10 r:9
e ng	k:103 g:75 th:17
ay f	er:59 uh:49 s:20 l:18 ee:17 i:13 t:10 ay:8
z a	n:67 k:49 m:32 s:16 p:11 g:10 j:9
h y	oo:193
d ow	n:126 t:29 d:16 s:12 uh:9
oo g	uh:73 er:32 o:24 l:19 aa:17 ee:16 ay:11
ay j	uh:43 i:42 er:30 ee:20 d:19 e:14 a:11 aa:9
oo i	ng:60 t:38 n:32 s:18 z:12 sh:11 d:9 l:8
th r	uh:37 i:36 o:35 e:21 aa:16 ay:16 oo:15 ee:11
e j	uh:69 i:41 oo:21 y:13 er:11 w:11 ee:10 d:8
j aa	n:73 r:35 k:20 b:16 s:16 m:12 l:10
or k	s:53 uh:31 i:24 er:16 t:15 ee:13 w:11 l:9 y:9
m s	t:91 uh:52 k:18 ee:11 ay:8
z l	ee:67 i:31 er:30 uh:22 aa:12 ay:9 or:9
o f	ee:29 er:26 i:26 l:21 ay:18 uh:18 aa:13 a:10 o:8 r:8
ch ay	n:58 l:33 z:21 m:19 s:16 k:11 d:9 v:9
b ow	m:74 n:49 er:32 t:20
aa j	i:87 ee:33 uh:29 er:17 e:8
d n	er:53 uh:48 ee:28 i:25 a:11 aa:9
th ee	uh:52 er:27 s:21 z:16 n:15 l:13 i:11 m:10 t:9
sh r	ay:35 e:21 ee:20 i:18 o:17 uh:17 a:13 oo:12 aa:11 ow:8
er a	n:52 k:38 l:18 s:18 m:16 t:11 d:10 ng:8
g z	a:61 i:33 e:27 or:18 b:13 aa:9 uh:8
l g	r:44 uh:35 er:28 aa:20 ay:15 i:10 e:9 o:8
th er	m:34 z:23 d:17 t:17 n:14 s:13 uh:13 o:11 i:9 ay:8 ee:8
ch oo	uh:53 ay:26 l:18 r:18 e:14 n:13 z:13 k:9
dh er	z:68 l:26 i:23 n:16 d:12 b:10 s:9
ee ch	ee:53 i:34 uh:28 er:26 t:12 e:11
er sh	i:61 uh:53 aa:13 a:10 m:9 n:9 e:8
r p	uh:40 er:34 ee:31 s:27 l:21 i:10
th i	ng:54 k:33 n:24 s:16 r:15 l:11 b:9
d b	er:32 e:24 a:20 or:15 ee:14 ay:13 l:13 aa:10 oo:10 r:10
l w	ay:36 i:35 er:33 oo:27 e:20 ee:10
aa ch	ee:59 uh:31 e:26 i:22 o:14 er:8
a z	uh:50 m:34 er:18 ee:17 i:14 b:9 l:9 d:8
m l	ee:62 i:28 uh:28 er:26 ay:15
ng t	uh:159
r j	uh:48 i:37 ee:30 er:14 d:13 a:9 e:8
oo ch	ee:108 er:24 e:9 i:9 oo:8
or d	ee:45 uh:41 i:27 er:14 k:13 r:8 s:8
y er	z:38 uh:31 d:23 ee:22 ay:20 i:12 k:10
a th	uh:50 ee:25 l:21 i:17 er:16 r:15 y:9
t z	uh:75 er:54 i:16 ee:8
ch aa	r:133 p:18
o h	a:28 aa:24 ay:20 i:20 ee:17 e:16 uh:15 o:11
p ow	er:43 n:38 t:19 l:15 r:9 d:8 s:8 z:8
oy n	t:88 i:14 z:14 d:13 er:9 uh:9
r v	uh:41 i:39 ee:20 er:19 aa:12 e:12
sh n	uh:61 er:25 ay:23 i:23 e:11
uh th	ee:31 e:21 aa:20 ay:19 uh:17 er:14 or:12 s:9
a j	uh:62 i:40 er:20 oo:11 ee:8
k h	o:30 aa:22 or:19 ow:18 e:13 uh:12 a:10 er:9 ay:8
sh l	ee:35 ay:23 i:16 a:13 aa:13 e:13 er:10 uh:10 or:8
d v	er:34 a:24 ay:24 e:23 uh:16 i:11 y:8
g y	oo:59 uh:51 er:30
l z	b:36 er:23 w:17 m:16 uh:15 i:14 ee:11 aa:8
oo v	ee:36 uh:35 er:28 i:18 z:12 d:11
s ch	uh:49 er:26 e:17 aa:16 ay:13 ee:10 oo:9
y e	l:46 r:32 n:26 s:18 t:18
t f	uh:57 i:19 or:19 ee:18 er:10 e:8 l:8
d k	a:24 i:21 ee:19 uh:15 w:14 o:13 aa:10 or:10 r:8
r f	uh:41 i:25 ee:20 er:12 ay:10 o:9 s:9 r:8
l h	aa:22 uh:22 ay:21 e:18 ow:16 er:12 i:12 or:10
er h	o:19 ow:19 or:17 aa:14 e:12 ee:12 oo:11 uh:10 ay:9 a:8
sh er	z:42 t:17 l:14 m:14 ay:12 b:12 d:10 i:9
sh m	uh:67 i:28 e:15 ay:10 ee:10
ee h	o:20 ay:18 aa:17 uh:17 a:13 i:10 ee:9 oo:9 e:8 or:8
oo sh	uh:93 ee:25 i:11
ng l	ee:109 uh:10 i:8
ee j	uh:52 ee:31 i:18 e:16 er:8
l p	uh:27 er:22 i:19 r:11 ay:10 t:10 ee:9 l:8 s:8
n th	uh:43 ee:22 r:15 aa:14 or:11 s:11 er:8
r w	i:32 er:20 ay:19 e:17 oo:17 or:10 ee:8
g m	uh:88 e:14 a:11 ay:9
j o	l:24 n:23 v:21 k:16 z:15 s:13 h:10
v r	uh:46 ee:34 i:32 o:10
t ow	n:94 t:18 er:9
th or	r:90 l:19 t:11
l n	uh:36 er:35 i:21 e:17 ee:10
w a	k:53 g:50 ng:16
er ch	i:36 uh:35 er:19 aa:11 oo:9 ay:8
er r	ee:33 ay:26 i:17 uh:17 o:13 e:10
sh o	n:31 l:28 b:17 f:12 k:10 m:9 p:9
sh or	r:89 n:15 l:12
uh r	ee:25 ay:18 a:15 e:14 o:14 aa:12 i:10 uh:8
er o	n:47 l:22 s:18 z:18 d:8
sh w	or:36 ay:21 e:21 i:17 er:10 aa:8
j ay	z:24 n:19 m:16 k:15 r:15 l:12 uh:11
p oy	n:92 z:11 l:9
aa sh	ee:36 uh:31 i:25 er:11 t:8
n g	r:41 uh:19 ay:17 l:14 aa:10 o:10
w o	l:51 t:18 m:13 n:10 v:10 k:9
er w	i:28 er:27 ay:21 e:19 oo:15
m f	uh:37 er:25 r:15 i:11 e:10 ee:10
o uh	n:59 l:20 z:12 s:9 t:8
r ch	uh:27 er:22 e:19 oo:14 ee:13 i:13
l oy	d:30 t:16 uh:14 er:13 s:13 m:10 z:10
z n	ee:31 er:28 i:26 uh:21
e th	uh:32 i:22 er:20 n:13 ee:9 l:8
g ee	n:26 z:26 s:16 uh:14 l:11 d:10
i th	uh:26 s:24 ee:14 i:13 er:10 d:8 r:8
d h	o:21 e:14 ow:13 aa:11 i:11 or:11 uh:11 ay:10
or ng	g:28 z:19 k:15 er:12 d:11 h:9 s:8
o i	ng:71 n:16 k:14
ee w	ay:34 er:14 uh:12 e:11 ee:11 or:10 aa:8
or g	uh:26 z:26 er:13 i:10 l:9 d:8 y:8
k ch	er:65 oo:34
p sh	uh:99
t b	or:24 r:17 er:15 a:14 e:13 ee:8 oo:8
y o	k:23 sh:18 d:14 s:11 g:8 h:8 l:8 z:8
k b	r:24 er:22 ay:15 oo:11 uh:9 a:8 or:8
y a	n:41 ng:23 k:22 s:11
r oy	d:33 t:21 l:17 s:13 uh:12
z oo	m:29 k:22 l:22 r:13 b:9
ee sh	uh:40 ee:37 i:9 y:8
i h	a:22 ay:18 e:17 aa:16 ee:11 er:10
k f	ee:20 er:20 a:12 e:9 aa:8 ay:8 or:8 uh:8
e dh	er:92
l r	i:21 ay:14 ee:14 o:14 oy:13 a:8 uh:8
th e	t:37 r:27 n:17 l:11
e ch	uh:30 i:22 ee:15 er:15 t:8
t h	ow:29 or:20 e:16 o:15 a:9
v or	r:55 l:20 n:13
er z	i:41 uh:14 b:11 e:11 ee:10
m k	o:27 i:13 w:13 or:12 ee:11 uh:10
ow d	er:25 ee:20 i:20 uh:20
r sh	uh:73 i:12
uh dh	er:85
v y	oo:63 er:22
j or	r:66 s:10 n:8
aa w	uh:50 aa:19 e:14
k d	ay:16 aa:14 ow:14 uh:12 oo:10 o:9 er:8
oo e	l:20 s:19 r:18 t:14 n:12
p k	i:30 uh:19 o:12 aa:11 ee:10
r th	uh:31 ee:17 r:14 w:12 s:8
n ow	n:35 r:15 s:12 er:11 t:8
d f	er:24 i:16 e:15 ee:13 aa:12
ow z	uh:28 er:26 i:16 d:9
s ow	th:42 n:24 er:13
aa h	uh:19 aa:17 ee:14 oo:12 er:8 i:8
b oy	l:27 z:18 s:13 k:11 t:9
oo f	er:15 uh:13 o:12 i:10 t:10 ee:9 s:9
oo j	ee:40 i:22 uh:15
oy l	z:20 uh:17 er:15 d:13 i:12
t y	oo:36 aa:12 uh:11 er:10 e:8
y or	r:53 n:14 ng:10
m r	uh:28 o:14 aa:12 ee:11 i:11
z w	er:39 ay:25 i:12
oo ay	t:48 sh:27
z o	n:29 l:25 z:13 m:8
ay o	s:23 l:14 m:14 n:12 t:11
ee or	r:55 n:11 l:8
m ow	n:47 s:14 th:12
oy s	t:32 uh:11 ee:10 i:10 er:9
n p	r:21 l:18 ay:16 ee:8 or:8
m n	uh:29 i:16 ee:15 ay:10
ay e	n:15 l:14 s:14 f:9 t:9 m:8
l ch	er:37 i:14 uh:9 e:8
ow uh	l:35 n:33
z or	r:52 n:8 s:8
i zh	uh:66
ee er	z:46 ay:19
z d	ay:31 uh:16 ee:10 or:8
ow er	z:35 d:19 i:10
l j	i:20 ee:16 uh:14 er:13
ng h	a:37 ow:15 or:10
or th	er:23 e:11 s:9 b:8 uh:8
th aa	l:27 r:12 m:11 n:9
d z	m:23 ee:14 i:13 b:8
ow l	i:22 z:11 uh:9 ee:8 t:8
o ch	ee:18 i:18 e:11 uh:10
ay a	n:27 d:12 k:9 g:8
r h	ow:19 aa:14 e:13 ee:10
d y	oo:37 uh:16
o j	e:23 ee:11 uh:10 i:9
oo er	z:28 d:13 t:12
or z	m:14 uh:12 b:10 ee:9 i:8
p n	er:15 uh:15 i:14 ee:8
s r	ee:19 i:13 ay:10 uh:10
th a	l:12 ng:12 k:10 ch:9 n:9
v n	uh:30 i:11 er:10
ay aa	n:16 r:10 g:8 l:8 t:8
s h	o:16 a:12 aa:11 ay:11
ay zh	uh:41 er:8
f ow	n:33 l:16
s d	uh:24 ay:17 ee:8
ay ng	g:34 k:14
m z	ee:16 i:14 b:10 l:8
ch m	uh:36 aa:11
j m	uh:47
oo zh	uh:47
ng uh	n:24 m:12 l:10
b z	er:18 or:11 aa:8 i:8
er or	f:19 l:14 n:12
j oy	n:34 s:11
b m	er:18 uh:15 i:11
er th	uh:13 s:12 r:9 or:8
ng s	t:42
ee y	aa:18 oo:15 uh:8
s v	i:29 e:12
th l	ee:21 i:10 uh:10
g s	t:40
p m	uh:40
aa th	uh:27 er:12
oy d	z:23 uh:16
p f	er:22 uh:17
v oy	s:17 d:14 t:8
f n	er:28 uh:10
oo aa	n:25 r:13
m t	r:10 ay:9 e:9 ee:9
th ay	l:10 z:10 n:9 uh:8
f m	uh:36
i dh	er:26 uh:10
m oy	l:10 er:9 n:9 s:8
or m	uh:26 i:10
or p	uh:14 er:11 f:11
s g	r:19 ay:9 uh:8
aa dh	er:35
o y	uh:14 aa:11 o:10
oy uh	l:22 n:13
th w	ay:14 e:12 er:9
aa y	aa:20 uh:14
b k	aa:13 uh:13 a:8
a dh	er:33
ay w	ay:16 er:9 or:8
ow r	ee:15 z:10 i:8
oy t	er:16 uh:9 s:8
t p	a:12 ay:12 o:9
aa zh	d:12 er:10 i:10
t g	er:11 r:11 uh:10
ay h	uh:12 aa:10 ee:9
e h	uh:18 er:13
er er	z:31
r z	uh:21 i:10
ay y	o:11 aa:10 uh:9
b w	aa:13 e:9 ay:8
k oy	n:17 l:13
uh y	oo:13 aa:9 ay:8
v s	k:30
zh er	z:18 ee:12
e zh	er:29
ee th	uh:12 i:9 ee:8
er zh	uh:29
r er	z:29
sh b	er:13 aa:8 or:8
ch o	k:10 n:10 l:8
ch or	n:11 r:9 f:8
o zh	er:19 uh:9
l sh	uh:18 i:9
ng i	ng:27
o er	z:27
or b	uh:11 er:8 r:8
a h	uh:26
ay or	r:18 n:8
b j	e:26
ee zh	uh:26
er dh	ee:26
b d	uh:25
g d	uh:25
ng m	uh:17 ay:8
oo h	aa:17 uh:8
s th	e:17 ee:8
ay ch	er:14 uh:10
er oo	n:8 t:8 z:8
ng er	z:24
o w	ay:14 er:10
p ch	er:24
ch l	er:12 ee:11
g b	ee:13 er:10
n oy	z:12 d:11
oo ee	z:23
ow i	ng:13 n:10
b t	ay:11 r:11
ch f	ee:13 er:9
ow m	uh:22
y i	r:22
z v	i:22
dh e	r:21
t d	ow:13 ay:8
ow th	w:11 ee:9
d oy	ch:10 n:9
s y	oo:19
z t	ow:10 e:9
zh i	n:19
b n	er:18
ch r	uh:18
ow b	er:18
s s	p:10 t:8
th m	uh:18
th o	l:10 m:8
dh i	ng:17
f g	aa:9 a:8
g ow	uh:9 t:8
oo th	s:9 er:8
z k	r:9 aa:8
z r	uh:9 ay:8
m d	ay:8 uh:8
m g	aa:16
or i	ng:16
sh k	uh:16
sh y	uh:16
th y	oo:16
oy m	uh:15
v m	uh:15
d g	r:14
dh uh	m:14
ee ng	g:14
i w	i:14
j y	oo:14
l th	ee:14
ng b	er:14
oo w	ay:14
or sh	uh:14
oy i	ng:14
aa ee	n:13
f k	o:13
m w	er:13
ow ee	a:13
oy er	z:13
t v	i:13
th f	uh:13
v w	aa:13
zh w	uh:13
d sh	i:12
ee oo	n:12
j l	ee:12
k v	i:12
o ng	g:12
oo a	l:12
p d	ay:12
p g	r:12
t oy	l:12
t t	ay:12
th s	t:12
ay ee	n:11
dh ee	uh:11
f oy	l:11
f w	e:11
ow g	uh:11
ch k	o:10
d p	a:10
er ow	n:10
er oy	d:10
n n	uh:10
o aa	p:10
or y	er:10
p w	e:10
v oo	r:10
z g	r:10
aa o	l:9
k p	aa:9
m h	ow:9
o or	r:9
o r	ay:9
oo y	aa:9
ow ch	i:9
oy z	uh:9
v k	o:9
a zh	uh:8
ay ow	t:8
ay th	ee:8
b v	er:8
ch n	ee:8
ch t	uh:8
e w	e:8
er y	oo:8
g oy	n:8
m th	i:8
m v	e:8
ng d	uh:8
ng f	ee:8
ng th	uh:8
o e	l:8
ow k	er:8
r dh	er:8
sh t	a:8
th b	er:8
th ow	z:8
zh d	uh:8
end trigrams
)FORMANT_NGRAM";
    return kTable;
}

} // namespace formant::detail
