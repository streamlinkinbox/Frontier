# East-Asian Roof Research — verified sources for the procedural roof generator

This document records the research the roof generator is built on. Every rule that
appears in code has a source here. Where sources disagree, the disagreement is noted and
the choice made in code is stated.

---

## 1. Roof taxonomy

### 1.1 Chinese (the structural parent system)

| Chinese | Pinyin | English | Character |
|---|---|---|---|
| 庑殿 | wǔdiàn | hipped roof | 4 slopes, no gable, ridge along the long axis; highest rank |
| 歇山 | xiēshān | hip-and-gable roof | hip skirt below, gable above; 2nd rank |
| 悬山 | xuánshān | overhanging gable roof | 2 slopes, gable + roof oversails the wall ("bargeboard" 博风板) |
| 硬山 | yìngshān | flush gable roof | 2 slopes, gable flush with the wall |
| 攒尖 | cuánjiān | pyramidal / pointed roof | slopes mass to a single point, capped with 宝顶 finial, **no ridge (无正脊)** |
| 卷棚 | juǎnpéng | rounded ridge | ridge replaced by a smooth round cap, no 正脊 |
| 重檐 | chóngyán | double eave | a second, lower eave tier wrapped around the building |

Source: Buildings 2025, 15(14), 2582, *Research on the Causes of the Concave Shapes of
Traditional Chinese Building Roofs*, Appendix A; Architectura Sinica s.v. `cuánjiān` (k000188).

### 1.2 Japanese (same geometry, different names)

- **切妻 kirizuma** — gabled roof (≈ 悬山/硬山)
- **寄棟 yosemune** — hipped roof (≈ 庑殿)
- **入母屋 irimoya** — hip-and-gable (≈ 歇山). *Two distinct derivations exist:* the
  aristocratic/temple irimoya is a **gable roof with hip eaves (hisashi) added around the
  moya core**; the minka irimoya is a **hipped roof with windows opened under the ridge**.
  (Higashino, *Japanese Traditional Architecture and Its Roofs: Gables Used as Social Icons*.)
- **方形造 / 宝形造 hōgyō-zukuri** — pyramidal roof (≈ 攒尖), used over square halls
- **唐破風 karahafu** — cusped/undulating gable (decorative, gates & entrances)
- **千鳥破風 chidori-hafu** — small triangular dormer gable set on a larger slope
- **裳階 moya / 下檐 hisashi** — the pent lower eave tier (≈ 重檐)

Source: Byakko Japan, *The Charm of Traditional Japanese Houses*; note.com *On the Roof
Shapes of Traditional Japanese Houses*; Higashino (academia.edu/36779717).

**Decision in code:** one geometry kernel, two label sets. The roof *forms* are shared, so
`roofType` selects geometry and a `style` flag only changes the naming, tile default and
ornament set.

---

## 2. The roof curve — 舉折 jǔzhé (the single most important rule)

Sources: *Yingzao Fashi* (李誡 Li Jie, 1103) juan 5 大木作制度, as transmitted by
Liang Sicheng; Architectura Sinica s.v. `jǔzhé` (k000223); Shen et al., *Parameterizing the
Curvilinear Roofs of Traditional Chinese Architecture* (2020); *Computing Chinese
Architecture*, Springer 2025; MDPI *Religions* 12(11):985.

Juzhe ("raise and bend") is literally an **algorithm** with two steps, performed in order:

### Step 1 — 舉屋 jǔwū ("raise the roof"): fix the ridge height

Let **B** = the distance between the centre lines of the front and rear eave purlins
(檐檩 / 橑檐枋). Then the ridge purlin (脊槫) is raised to

```
H0 = B / 3      for 殿阁 (palace/hall) class
H0 = B / 4      for 厅堂 (ordinary hall) class
H0 ≈ B / 4 or B / 5   smaller buildings, with an extra 8/100 h correction in some
                      Annotated Yingzao Fashi readings (see note below)
```

The *Parameterizing* paper gives H0 = h + X with X = (c/100)·h, i.e. H0 = h·(1 + c/100),
and identifies an error in the *Annotated Yingzao Fashi* drawing sample (X = cB/100 should
be X = ch/100). Code exposes H0 as `riseRatio = H0 / B` with defaults **1/3, 1/4, 1/5**.

### Step 2 — 折屋 zhéwū ("bend the roof"): depress every purlin below the ridge

```
divide the half-span B/2 into m equal horizontal steps  (m = number of rafter steps 步架)
draw the working line from the ridge purlin top straight down to the eave purlin top
drop the first purlin below the ridge by   z1 = H0 / 10
drop each following purlin by half the previous drop   zn = z1 / 2^(n-1)
re-draw the working line from the purlin you just dropped to the eave purlin, and repeat
```

Closed form (both the paper and Architectura Sinica agree):

```
H_n = ((m - n) / (m - n + 1)) · H_(n-1)  −  (1/2)^(n-1) · (1/10)·H0 ,   n = 1 … m
H_0 = ridge = H0          H_m = eave purlin = 0
```

`m` = number of purlin bays from the eave purlin to the ridge purlin.
**Maximum m = 7** (the Yingzao Fashi permits at most seven rafter steps; beyond that the
sweeping curve degenerates into a straight line — Shen et al.). Historically the result
gives an overall eave→ridge pitch between **1:3 and 1:1.5** across the 11 Tang/Song
buildings measured in the MDPI study — code asserts this band as a self-test.

### Step 3 — rafters sit *on* the purlins

Between each pair of adjacent purlins a rafter (椽 chuán) is laid. The purlins therefore
form the **concave polyline**, and the rafters are straight segments between them — exactly
why the traditional roof is polygonal, not an arc. Code does the same: the profile is a
polyline through the purlin points, never a spline.

---

## 3. Eave construction — 飛椽 flying rafters, and why the eave turns up

Source: MDPI *Buildings* 15(14):2582 §3.1 (quoting YZS juan 5).

- 檐椽 **yánchuán** (eave rafter): **round** section, runs from the eave purlin (檐檩) out
  past the wall.
- 飛椽 **fēichuán** (flying rafter): **square** section, width = 8/10 of the rafter
  diameter, thickness = 7/10. Its *tail* is nailed to the back of the eave rafter, its
  *head* sticks out further. Head : tail length generally **> 1:2** (tail is the longer
  piece).
- Because the flying rafter is nailed at an angle on top of the eave rafter, the eave line
  **kinks upward** where they meet. *This inverted kink is the origin of the concave eave
  curve — it is a construction artefact, not decoration.* YZS: a flying rafter extends
  **6 cun outward for each 1 chi** of eave projection (≈ 0.6 extra projection).
- YZS eave projection: 3 cun rafter → 3 chi 5 cun projection; 5 cun rafter → 4.0–4.5 chi.
  Code exposes `eaveProjection` as a ratio of the rafter span.
- 望板 **wàngbǎn** (sheathing boards) are laid over both rafter types; 飛椽壓尾 is the
  cut-down tail of the flying rafter that gives contact area on the eave rafter.

**Code rule (`no-float` law #1):** every flying rafter's tail endpoint is snapped onto the
top surface of its parent eave rafter — the two are built as a single polyline so they
cannot detach.

---

## 4. Corner construction — 角翹/翼角 corner upturn

Source: MDPI *Buildings* 15(14):2582 §3.2 and Figs 5–6.

- 大角梁 / 老角梁 **dà jiǎoliáng** (principal hip rafter) — plan runs at 45° from the corner
  column to the ridge; 子角梁 **zǐ jiǎoliáng** (upper hip rafter) continues it. The two
  together are the 角梁.
- The principal hip rafter sits **higher than the adjoining eave rafters**. To keep the roof
  surface continuous a triangular pad block — 襯頭木 / 生頭木 **chèntóu mù** — is inserted on
  the purlin's back near the corner.
- The 翼角檐椽 **wing-corner eave rafters** fan out in plan along the hip rafter, each
  progressively raised by the pad blocks: *"This progressively elevates the wing corner
  eave rafters, generating the corner's characteristic curvature."* Wing-corner rafters
  closer to the corner have **shorter tails** (their tails are cut because the fan
  converges — visible in the YZS plan drawing, Fig. 6b).
- Southern Chinese gardens use the sharper 嫩戗 **nènqiāng / 發戗** variant: a second,
  steeper short hip member set on the 老戗 to throw the corner up violently.

**Code rule (`no-float` law #2):** the hip rafter is generated as the true intersection
line of the two adjacent roof faces (§5), so it always lies *on* both surfaces by
construction. Wing-corner rafters are generated by rotating a rafter about the corner
column and then clamping its inner end onto the eave purlin arc so it stays supported.

---

## 5. Building the full 3-D surface without floating

The trick that makes this generator correct is to **never build faces separately.** A single
height field is defined over the plan:

```
s(x, z)  = min over every eave of ( perpendicular distance from that eave ) / ( that face's perpendicular depth )
h(s)     = the juzhe profile of §2, normalised so h(0)=0 at the eave purlin, h(1)=H0 at the ridge
H(x, z)  = H0 · h( s(x, z) )
```

Consequences, all of which are provable rather than tuned:

1. Each roof face's cross-section (measured perpendicular to its own eave) *is* the juzhe
   polyline → §2 satisfied on every face.
2. Where two faces meet, both compute the **same** s and therefore the **same** H → the hip
   ridge (垂脊/角梁) is exactly the intersection curve of the two faces. Hips are found, not
   guessed: **hips can never float and can never intersect incorrectly.**
3. The hip rafter's plan run is √2 × a face's run, so its slope is shallower by the same
   factor — which is the correct, and counter-intuitive, real behaviour used by carpenters
   (MDPI §3.2 notes the principal hip rafter's height differs only slightly from the eave
   rafters, so the corner needs pad blocks).

### 5.1 歇山 / 入母屋 irimoya break line

The skirt (下檐) is the height field of §5 restricted to the hip faces; the break line is
the locus where the *side* faces reach the level at which the gable (上部) begins. The
gable's triangular wall stands on that line — its bottom edge is horizontal and its base
width is found by solving `h(s) = h_break`. Below the break line the hip ridges (隅棟) run
diagonally; above it the 破風 bargeboards run straight to the ridge. Code solves the break
width numerically from the juzhe profile rather than assuming it, so the gable always
stands exactly on the skirt.

---

## 6. Tiles — 瓦 wǎ / 瓦 kawara

Sources: archinatour.com *Tiled Roof in Traditional Chinese Architecture*;
chineserooftile.com *Chinese Roof Tiles: History, Types & Modern Applications*; Grokipedia
*Chinese glazed roof tile*; byakko.co / note.com for the Japanese names.

### 6.1 The two-layer system (本瓦葺 hongawara-gae)

| Element | Chinese | Japanese | Role |
|---|---|---|---|
| pan / plate tile | 板瓦 bǎnwǎ | 平瓦 / 牝瓦 | concave, laid on the sheathing, the water channel |
| barrel / roll tile | 筒瓦 tǒngwǎ | 丸瓦 | half-cylinder, caps the seam between two pan tiles |
| eave round | 瓦當 wǎdāng | 軒丸瓦 | disc-faced tile closing each barrel row at the eave |
| drip tile | 滴水 dīshuǐ | 軒平瓦 | the pan tile at the eave edge, with a hanging apron to throw water clear of the timber |
| ridge tile | 脊瓦 jǐwǎ | 棟瓦 | caps ridges & hips |
| ogre tile | 脊獸 | 鬼瓦 onigawara | ornament at the ridge end |

### 6.2 Overlap rules (this is what makes rows look right)

- Barrel-tile rows going **up the slope**: 壓六露四 *yā liù lù sì* — "press six, expose
  four" → **40 % of each tile is exposed**, 60 % covered by the next one up. (Song standard
  overlap 40 %; Qing raised it to 70 % = 30 % exposed. Code exposes both as a preset.)
- Pan tiles across the slope: laid side by side, **one pan tile every barrel-tile pitch**,
  each pan tile concave-side up with its edges tucked under the neighbouring barrels.
- Pan tiles going up the slope: 壓七露三 — 30 % exposed (they are longer than barrels).

### 6.3 Glaze / colour — legal rank in imperial China

| Colour | Chinese | Permitted on |
|---|---|---|
| yellow 琉璃黄 | imperial yellow | imperial palaces & ancestral halls **only** |
| green | 琉璃绿 | temples, princes' mansions, gardens |
| blue | 琉璃蓝 | 天坛 Temple of Heaven, heaven-worship buildings |
| black / grey 青瓦 | unglazed grey-black | commoners, villages, courtyard houses, most of Japan |
| brown/unglazed 小青瓦 | grey, unglazed | widespread vernacular |

Japan adds 銀・銅・黒 silver/copper/black kawara, plus 桟瓦 sangawara — a single-layer
interlocking "J" tile invented in Japan (lighter, one layer instead of two).

---

## 7. What the generator therefore has to guarantee ("no floating")

1. **Tile → sheathing:** every tile's base is placed on the height field H(x,z) lifted by
   the tile's own thickness. Verified by ray-casting: every tile must hit the sheathing.
2. **Sheathing/rafter → purlin:** every rafter spans exactly from the top of purlin `n` to
   the top of purlin `n+1`. Never a free end.
3. **Flying rafter → eave rafter:** built as one polyline (§3).
4. **Purlin → beam/column:** purlin heights come from the juzhe solver; the columns and
   beams beneath are placed at the purlin plan positions, so each purlin always has a
   supporter directly under it.
5. **Hip → faces:** intersection of surfaces (§5), never a separate guessed curve.
6. **Ridge → king post → main beam:** the ridge purlin is shored by 侏儒柱/瓜柱 posts sitting
   on the 大梁 main beam, which itself lands on the columns.

---

## 8. Sources

1. *Research on the Causes of the Concave Shapes of Traditional Chinese Building Roofs from
   the Construction Perspective*, Buildings 2025, 15(14), 2582 — https://www.mdpi.com/2075-5309/15/14/2582
2. Shen, Y. et al., *Parameterizing the Curvilinear Roofs of Traditional Chinese
   Architecture* (2020) — https://www.researchgate.net/publication/343266609
3. Architectura Sinica, `jǔzhé` 舉折 (k000223) and `cuánjiān` 攢尖 (k000188) — https://architecturasinica.org
4. *Computing Chinese Architecture*, Springer 2025, ch. 24 — https://link.springer.com/chapter/10.1007/978-3-031-81623-9_24
5. *Rethinking the Proportional Design Principles of Timber-Framed Buddhist Buildings in the
   Goryeo Era*, Religions 12(11):985 — https://www.mdpi.com/2077-1444/12/11/985
6. Higashino, A. P., *Japanese Traditional Architecture and Its Roofs: Gables Used as Social
   Icons* — https://www.academia.edu/36779717
7. archinatour.com, *Tiled Roof in Traditional Chinese Architecture* — https://archinatour.com/tiled-roof/
8. chineserooftile.com, *Chinese Roof Tiles: History, Types & Modern Applications*
9. Grokipedia, *Chinese glazed roof tile*
10. Byakko Japan, *The Charm of Traditional Japanese Houses* — https://byakko.co/blogs/architecture-and-interior-design/the-charm-of-traditional-japanese-houses
