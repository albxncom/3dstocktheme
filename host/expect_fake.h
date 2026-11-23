/* What the host tests expect of the test-only look (host/look_fake.c) where it differs from Stock (host/expect.h):
 * it differs in both kinds, so test_look proves that test_ui's checks read a look's own expectations. */
#ifndef EXPECT_FAKE_H
#define EXPECT_FAKE_H

/* its wrench 2 px right of Stock's and 1 px higher (look_fake.c, tab_icon_x and tab_icon_y) */
#define EXPECT_FAKE_WRENCH_X 14
#define EXPECT_FAKE_WRENCH_Y 3

/* no pattern on the top screen: its own ground over the whole screen first (look_fake.c, ground) */
#define EXPECT_FAKE_PATTERN 0

#endif
