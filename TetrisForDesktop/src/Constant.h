#ifndef _CONSTANT_H
#define _CONSTANT_H

enum Boolean{
	False, True
};

enum Direction{
	UP, RIGHT, DOWN, LEFT
};

enum ProcessType{
	DIRECTION, DIRECT_DOWN, AUTO
};

enum KeyCode{
	UP_KEY_CODE = 72, LEFT_KEY_CODE = 75,
	RIGHT_KEY_CODE = 77, DOWN_KEY_CODE = 80,
	SPACE_BAR_KEY_CODE = 32, ESC_KEY_CODE = 27,
	L_KEY_CODE = 108, ENTER_KEY_CODE = 13,

	P_KEY_CODE = 112,		//아이템 : 한 줄 지우기
	O_KEY_CODE = 111,		//아이템 : 두 줄 지우기
	I_KEY_CODE = 105,		//아이템 : 전체 지우기
	X_KEY_CODE = 120		//다음블럭과 다다음블럭 바꾸기
};

enum FontColor{
	BLACK, BLUE, GREEN, JADE, RED, PURPLE, YELLOW, WHITE, GRAY,
	LIGHT_BLUE, LIGHT_GREEN, LIGHT_JADE, LIGHT_RED, LIGHT_PURPLE, LIGHT_YELLOW, LIGHT_WHITE, WHITE_INVERSION = 240
};

enum BoardType{
	EMPTY,
	MOVING_BLOCK, FIXED_BLOCK, SHADOW_BLOCK,
	LEFT_WALL, RIGHT_WALL, BOTTOM_WALL, TOP_WALL,
	LEFT_TOP_EDGE, RIGHT_TOP_EDGE, LEFT_BOTTOM_EDGE, RIGHT_BOTTOM_EDGE
};

enum MainMenu{
	START_MAIN_MENU = 1, RANKING_MAIN_MENU, SETTING_MAIN_MENU, EXIT_MAIN_MENU
};

enum PauseMenu{
	RESUME_PAUSE_MENU = 1, MAIN_MENU_PAUSE_MENU
};

enum EndMenu{
	RANKING_END_MENU = 1, MAIN_MENU_END_MENU, EXIT_END_MENU
};

#define MAX_SPEED_LEVEL 10
#define MIN_SPEED_LEVEL 1

#define DEFAULT_FONT_COLOR LIGHT_WHITE

#endif