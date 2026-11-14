# Studio font

`LuaCStudioMono.ttf` is a static, subsetted Noto Sans Mono Regular from
[Google Fonts](https://github.com/google/fonts/tree/29b4ec6df2522a8aee279414b78a07910b065874/ofl/notosansmono).
Copyright 2022 The Noto Project Authors. It is distributed under the SIL Open
Font License 1.1 in `FONT-LICENSE.txt`.

The source variable font was instantiated at weight 400 and width 100 with
fontTools, then subset to U+0020–U+024F, U+0370–U+052F, U+2000–U+206F,
U+20A0–U+20CF, U+2190–U+21FF and U+FFFD (where present in the source).
`font_studio_mono.hpp` embeds those bytes and the ranges actually in its cmap;
missing codepoints use the `?` glyph instead of an empty baked slot.
