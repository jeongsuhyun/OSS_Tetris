#include <stdio.h>
#include <process.h>
#include <string.h>
#include <windows.h>
#include <time.h>
#include "TetrisManager.h"
#include "Util.h"
#include "Constant.h"

#define MAX_MAKE_OBSTACLE_ONE_LINE_COUNT 2
#define MILLI_SECONDS_PER_SECOND 1000
#define INITIAL_SPEED 300
#define SPEED_LEVEL_OFFSET 40
#define LEVELP_UP_CONDITION 3
#define STATUS_POSITION_X_TO_PRINT 38
#define STATUS_POSITION_Y_TO_PRINT 1

#define LINES_TO_DELETE_HIGHTING_COUNT 3
#define LINES_TO_DELETE_HIGHTING_MILLISECOND 100

#define BOARD_TYPES_TO_PRINT_ROW_SIZE 12
#define BOARD_TYPES_TO_PRINT_COL_SIZE 3

static const char boardTypesToPrint[BOARD_TYPES_TO_PRINT_ROW_SIZE][BOARD_TYPES_TO_PRINT_COL_SIZE] = {
	("  "), ("■"), ("▩"), ("□"), ("┃"), ("┃"), ("━"), ("━"), ("┏"), ("┓"), ("┗"), ("┛")
};

static void _TetrisManager_PrintStatus(TetrisManager* tetrisManager, int x, int y);
static void _TetrisManager_PrintKeys(TetrisManager* tetrisManager, int x, int y);
static void _TetrisManager_PrintBlock(TetrisManager* tetrisManager, int blockType, int status);
static void _TetrisManager_InitBoard(TetrisManager* tetrisManager);
static void _TetrisManager_UpSpeedLevel(TetrisManager* tetrisManager);
static void _TetrisManager_SearchLineIndexesToDelete(TetrisManager* tetrisManager, int* indexes, int* count);
static void _TetrisManager_DeleteLines(TetrisManager* tetrisManager, int* indexes, int count);
static void _TetrisManager_HighlightLinesToDelete(TetrisManager* tetrisManager, int* indexes, int count);
static Block _TetrisManager_GetBlockByType(TetrisManager* tetrisManager, int blockType);
static void _TetrisManager_MakeShadow(TetrisManager* tetrisManager);
static int _TetrisManager_CheckValidPosition(TetrisManager* tetrisManager, int blockType, int direction);
static void _TetrisManager_ChangeBoardByDirection(TetrisManager* tetrisManager, int blockType, int direction);
static void _TetrisManager_ChangeBoardByStatus(TetrisManager* tetrisManager, int blockType, int status);
static DWORD WINAPI _TetrisManager_OnTotalTimeThreadStarted(void *tetrisManager);
static void _TetrisManager_PrintTotalTime(TetrisManager tetrisManager);
static void _TetrisManager_MakeObstacleOneLine(TetrisManager* tetrisManager);

//아이템에 의해 줄이 제거되는 경우 점수와 레벨에 반영되지 않는다.
static void _TetrisManager_Item_DeleteLines(TetrisManager* tetrisManager, int* indexes, int count);

//게임 도중 board의 테두리의 형태를 유지시킨다.
static void TetrisManager_MaintainBoard(TetrisManager* tetrisManager);

//줄이 제거 된 후 그림자를 처리한다.
static void TetrisManager_Item_ProcessBLOCK(TetrisManager* tetrisManager, int blockType, int number);

//전체 줄 제거하는 아이템 사용할 때, 행 전체를 indexes에 담고 행 갯수를 세어 count에 담는다.
static void TetrisManager_SearchAllLineIndexesToDelete(TetrisManager* tetrisManager, int* indexes, int* count);

//다음블럭과 다다음블럭을 바꿀 수 있는 횟수를 레벨에 맞게 수정한다.
void TetrisManager_InitializeNextCount(TetrisManager* tetrisManager, int speedLevel);	

void TetrisManager_Init(TetrisManager* tetrisManager, int speedLevel){
	Block block;
	block.current = -1;
	_TetrisManager_InitBoard(tetrisManager);
	tetrisManager->block = Block_Make(True, block);
	tetrisManager->shadow = tetrisManager->block;
	tetrisManager->isHoldAvailable = True;
	_TetrisManager_MakeShadow(tetrisManager);
	tetrisManager->deletedLineCount = 0;
	tetrisManager->speedLevel = speedLevel;
	tetrisManager->score = 0;
	tetrisManager->totalTimeThread = NULL;
	tetrisManager->totalTime = 0;
	tetrisManager->isTotalTimeAvailable = False;

	//다음블럭과 다다음블럭을 바꿀수 있는 횟수와 다음블록 숨기는 주기를 초기화
	TetrisManager_InitializeNextCount(tetrisManager,tetrisManager->speedLevel);		

	//쓰레드 lock을 위해 Mutex초기화
	tetrisManager->mutex=CreateMutex(NULL, FALSE, NULL);

	//현재 상태가 다음블럭이 숨겨져있는 상태인지 아닌지를 체크
	tetrisManager->checkBlindStatus=False;	//blind상태가 아니다.
}

void TetrisManager_ProcessDirection(TetrisManager* tetrisManager, int direction){
	if (direction != DOWN){
		_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, EMPTY);
		_TetrisManager_ChangeBoardByStatus(tetrisManager, SHADOW_BLOCK, EMPTY);
	}
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);
	_TetrisManager_ChangeBoardByDirection(tetrisManager, MOVING_BLOCK, direction);
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, MOVING_BLOCK);
	if (direction != DOWN){
		_TetrisManager_MakeShadow(tetrisManager);
	}
}

void TetrisManager_ProcessAuto(TetrisManager* tetrisManager){
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);
	_TetrisManager_ChangeBoardByDirection(tetrisManager, MOVING_BLOCK, DOWN);
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, MOVING_BLOCK);
}

void TetrisManager_ProcessDirectDown(TetrisManager* tetrisManager){
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);
	while (!TetrisManager_IsReachedToBottom(tetrisManager, MOVING_BLOCK)){
		_TetrisManager_ChangeBoardByDirection(tetrisManager, MOVING_BLOCK, DOWN);
	}
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, MOVING_BLOCK);
}

void TetrisManager_ProcessDeletingLines(TetrisManager* tetrisManager){
	int indexes[BOARD_ROW_SIZE];
	int count;

	// use temp size (magic number)
	int x = 38;
	int y = 1;

	_TetrisManager_SearchLineIndexesToDelete(tetrisManager, indexes, &count);	//삭제해야 할 행의 번호들을 indexes에 담는다.
	if (count > 0){

		//during hightlighting the lines to delete, hide moving block and shadow block
		_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, EMPTY);
		_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);
		_TetrisManager_HighlightLinesToDelete(tetrisManager, indexes, count);	//삭제할 행을 깜빡인다.
		_TetrisManager_DeleteLines(tetrisManager, indexes, count);
		_TetrisManager_ChangeBoardByStatus(tetrisManager, MOVING_BLOCK, MOVING_BLOCK);
		TetrisManager_PrintBoard(tetrisManager);
		_TetrisManager_PrintStatus(tetrisManager, x, y);
	}
}

int TetrisManager_IsReachedToBottom(TetrisManager* tetrisManager, int blockType){
	int i;
	Block block = _TetrisManager_GetBlockByType(tetrisManager, blockType);
	for (i = 0; i < POSITIONS_SIZE; i++){
		int x = Block_GetPositions(block)[i].x;
		int y = Block_GetPositions(block)[i].y;
		if (!(tetrisManager->board[x + 1][y] == EMPTY || tetrisManager->board[x + 1][y] == MOVING_BLOCK || tetrisManager->board[x + 1][y] == SHADOW_BLOCK)){
			return True;
		}
	}
	return False;
}

int TetrisManager_ProcessReachedCase(TetrisManager* tetrisManager){

	// use temp size (magic number)
	int x = 40;
	int y = 15;

	// if this variable equals to 
	static int makeObstacleOneLineCount = 0;

	_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, EMPTY);
	_TetrisManager_ChangeBoardByStatus(tetrisManager, SHADOW_BLOCK, EMPTY);
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);
	_TetrisManager_ChangeBoardByStatus(tetrisManager, MOVING_BLOCK, FIXED_BLOCK);
	_TetrisManager_PrintBlock(tetrisManager, FIXED_BLOCK, FIXED_BLOCK);
	tetrisManager->block = Block_Make(False, tetrisManager->block);
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, MOVING_BLOCK);
	_TetrisManager_MakeShadow(tetrisManager);
	if (makeObstacleOneLineCount == MAX_MAKE_OBSTACLE_ONE_LINE_COUNT){
		if (tetrisManager->speedLevel == MAX_SPEED_LEVEL){
			_TetrisManager_MakeObstacleOneLine(tetrisManager);
		}
		makeObstacleOneLineCount = 0;
	} else{
		makeObstacleOneLineCount++;
	}
	/*Block_PrintNext(tetrisManager->block, 0, x, y);
	x += 20;
	Block_PrintNext(tetrisManager->block, 1, x, y);*/
	tetrisManager->isHoldAvailable = True;
	if (TetrisManager_IsReachedToBottom(tetrisManager, MOVING_BLOCK)){
		Block_Destroy(tetrisManager->block);
		return END;
	}
	else{
		return PLAYING;
	}
}

void TetrisManager_PrintBoard(TetrisManager* tetrisManager){
	int i;
	int j;
	int x = 0;
	int y = 0;
	for (i = 0; i < BOARD_ROW_SIZE; i++){
		WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		//LOCK 걸기(다른 부분에서 커서의 색상을 변경하는 것을 막기위해 임계구역으로 보호)
		CursorUtil_GotoXY(x, y++);
		for (j = 0; j < BOARD_COL_SIZE; j++){
			switch (tetrisManager->board[i][j]){
			case LEFT_TOP_EDGE: case RIGHT_TOP_EDGE: case LEFT_BOTTOM_EDGE: case RIGHT_BOTTOM_EDGE:
			case LEFT_WALL: case RIGHT_WALL: case TOP_WALL: case BOTTOM_WALL:
			case EMPTY:
				FontUtil_ChangeFontColor(DEFAULT_FONT_COLOR);
				break;
			case MOVING_BLOCK:
				FontUtil_ChangeFontColor(tetrisManager->block.color);
				break;
			case FIXED_BLOCK:
				FontUtil_ChangeFontColor(WHITE);
				break;
			case SHADOW_BLOCK:
				FontUtil_ChangeFontColor(GRAY);
				break;
			}
			printf("%s", boardTypesToPrint[tetrisManager->board[i][j]]);
			FontUtil_ChangeFontColor(DEFAULT_FONT_COLOR);	//매개변수로 color 를 받아서 해당 color 로 출력 커서의 색상을 변경하는 역할을 합니다. 
															//SetConsoleTextAttribute 함수를 호출하면서 매개변수로 변경할 색상 정수값을 넘겨 출력 커서의 색상을 변경합니다.
		}
		ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
	}
}

void TetrisManager_PrintDetailInfomation(TetrisManager* tetrisManager){
	int x = STATUS_POSITION_X_TO_PRINT;
	int y = STATUS_POSITION_Y_TO_PRINT;
	_TetrisManager_PrintStatus(tetrisManager, x, y);
	x += 6;
	y += 4;
	_TetrisManager_PrintKeys(tetrisManager, x, y);
	x -= 4;
	y += 10;
	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
	Block_PrintNext(tetrisManager->block, 0, x, y);
	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
	x += 20;
	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
	Block_PrintNext(tetrisManager->block, 1, x, y);
	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
	y += 5;
	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
	Block_PrintHold(tetrisManager->block, x, y);
	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제

	TetrisManager_StartTotalTime(tetrisManager);
	_TetrisManager_PrintTotalTime(*tetrisManager);
}

DWORD TetrisManager_GetDownMilliSecond(TetrisManager* tetrisManager){
	int i;
	DWORD milliSecond = INITIAL_SPEED;
	for (i = MIN_SPEED_LEVEL; i < tetrisManager->speedLevel; i++){
		if (i < MAX_SPEED_LEVEL / 2){
			milliSecond -= SPEED_LEVEL_OFFSET;
		}
		else{
			milliSecond -= (SPEED_LEVEL_OFFSET / 5);
		}
	}
	return milliSecond;
}

void TetrisManager_MakeHold(TetrisManager* tetrisManager){

	// use temp size (magic number)
	int x = 60;
	int y = 20;

	if (tetrisManager->isHoldAvailable){
		_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);
		_TetrisManager_ChangeBoardByStatus(tetrisManager, MOVING_BLOCK, EMPTY);
		_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, EMPTY);
		_TetrisManager_ChangeBoardByStatus(tetrisManager, SHADOW_BLOCK, EMPTY);
		Block_ChangeCurrentForHold(&tetrisManager->block);
		tetrisManager->isHoldAvailable = False;

		WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
		Block_PrintHold(tetrisManager->block, x, y);
		ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제

		_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, MOVING_BLOCK);
		_TetrisManager_MakeShadow(tetrisManager);
	}
}

void TetrisManager_StartTotalTime(TetrisManager* tetrisManager){
	DWORD totalTimeThreadID;
	tetrisManager->isTotalTimeAvailable = True;
	tetrisManager->totalTimeThread = (HANDLE)_beginthreadex(NULL, 0, _TetrisManager_OnTotalTimeThreadStarted, tetrisManager, 0, (unsigned *)&totalTimeThreadID);
}

void TetrisManager_PauseTotalTime(TetrisManager* tetrisManager){
	tetrisManager->isTotalTimeAvailable = False;
	tetrisManager->totalTime--; // to show not one added time but paused time
}

void TetrisManager_StopTotalTime(TetrisManager* tetrisManager){
	tetrisManager->isTotalTimeAvailable = False;
	tetrisManager->totalTime = 0;
}

static void _TetrisManager_PrintStatus(TetrisManager* tetrisManager, int x, int y){
	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기

	ScreenUtil_ClearRectangle(x + 2, y + 1, 4, 1); // use temp size (magic number)
	ScreenUtil_ClearRectangle(x + 13, y + 1, 6, 1); // use temp size (magic number)
	ScreenUtil_ClearRectangle(x + 26, y + 1, 12, 1); // use temp size (magic number)
	CursorUtil_GotoXY(x, y++);
	printf("┏ Lv ┓   ┏ Line ┓   ┏ TotalScore ┓");
	CursorUtil_GotoXY(x, y++);
	printf("┃%3d ┃   ┃%4d  ┃   ┃%7d     ┃", tetrisManager->speedLevel, tetrisManager->deletedLineCount, tetrisManager->score);
	CursorUtil_GotoXY(x, y++);
	printf("┗━━┛   ┗━━━┛   ┗━━━━━━┛");

	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
}

static void _TetrisManager_PrintKeys(TetrisManager* tetrisManager, int x, int y){
	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기

	ScreenUtil_ClearRectangle(x, y, 26, 9); // use temp size (magic number)
	CursorUtil_GotoXY(x, y++);
	printf("┏━━━━ Keys ━━━━┓");
	CursorUtil_GotoXY(x, y++);
	printf("┃←       ┃move left  ┃");
	CursorUtil_GotoXY(x, y++);
	printf("┃→       ┃move right ┃");
	CursorUtil_GotoXY(x, y++);
	printf("┃↓       ┃move down  ┃");
	CursorUtil_GotoXY(x, y++);
	printf("┃↑       ┃rotate     ┃");
	CursorUtil_GotoXY(x, y++);
	printf("┃SpaceBar ┃direct down┃");
	CursorUtil_GotoXY(x, y++);
	printf("┃ESC      ┃pause      ┃");
	CursorUtil_GotoXY(x, y++);
	printf("┃L (l)    ┃hold       ┃");
	CursorUtil_GotoXY(x, y++);
	printf("┗━━━━━━━━━━━┛");

	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
}

static void _TetrisManager_PrintBlock(TetrisManager* tetrisManager, int blockType, int status){
	int i;
	Block block = _TetrisManager_GetBlockByType(tetrisManager, blockType);

	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
	switch (blockType){
	case MOVING_BLOCK:
		FontUtil_ChangeFontColor(tetrisManager->block.color);
		break;
	case FIXED_BLOCK:
		FontUtil_ChangeFontColor(WHITE);
		break;
	case SHADOW_BLOCK:
		FontUtil_ChangeFontColor(GRAY);
		break;
	}
	for (i = 0; i < POSITIONS_SIZE; i++){
		int x = Block_GetPositions(block)[i].x;
		int y = Block_GetPositions(block)[i].y;
		CursorUtil_GotoXY(2 * y, x);
		printf("%s", boardTypesToPrint[status]);
	}
	FontUtil_ChangeFontColor(DEFAULT_FONT_COLOR);
	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);			// LOCK 해제

	_TetrisManager_PrintTotalTime(*tetrisManager); // because of multi thread problem, this function covers total time
}

static void _TetrisManager_InitBoard(TetrisManager* tetrisManager){
	int i;
	memset(tetrisManager->board, EMPTY, sizeof(char)* BOARD_ROW_SIZE * BOARD_COL_SIZE);
	for (i = 0; i < BOARD_ROW_SIZE; i++){
		tetrisManager->board[i][0] = LEFT_WALL;
		tetrisManager->board[i][BOARD_COL_SIZE - 1] = RIGHT_WALL;
	}
	for (i = 0; i < BOARD_COL_SIZE; i++){
		tetrisManager->board[0][i] = TOP_WALL;
		tetrisManager->board[BOARD_ROW_SIZE - 1][i] = BOTTOM_WALL;
	}

	//in order to make center hole at top wall, we convert center top wall into empty intentionally
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2 - 1] = EMPTY;
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2] = EMPTY;
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2 + 1] = EMPTY;
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2 + 2] = EMPTY;

	tetrisManager->board[0][0] = LEFT_TOP_EDGE;
	tetrisManager->board[0][BOARD_COL_SIZE - 1] = RIGHT_TOP_EDGE;
	tetrisManager->board[BOARD_ROW_SIZE - 1][0] = LEFT_BOTTOM_EDGE;
	tetrisManager->board[BOARD_ROW_SIZE - 1][BOARD_COL_SIZE - 1] = RIGHT_BOTTOM_EDGE;
}

static void _TetrisManager_UpSpeedLevel(TetrisManager* tetrisManager){
	if (tetrisManager->speedLevel < MAX_SPEED_LEVEL){
		tetrisManager->speedLevel++;
				
		TetrisManager_InitializeNextCount(tetrisManager,tetrisManager->speedLevel);		//다음블럭과 다다음블럭을 바꿀수 있는 횟수를 다시 초기화해준다.
	}
}

static void _TetrisManager_SearchLineIndexesToDelete(TetrisManager* tetrisManager, int* indexes, int* count){
	int i;
	int j;
	int toDelete;
	memset(indexes, -1, sizeof(int)* (BOARD_ROW_SIZE - 2));
	*count = 0;
	for (i = 1; i < BOARD_ROW_SIZE - 1; i++){
		toDelete = True;
		for (j = 1; j < BOARD_COL_SIZE - 1; j++){
			if (tetrisManager->board[i][j] != FIXED_BLOCK){
				toDelete = False;
				break;
			}
		}
		if (toDelete){
			indexes[(*count)++] = i;
		}
	}
}

static void _TetrisManager_DeleteLines(TetrisManager* tetrisManager, int* indexes, int count){
	int i;
	int j;
	int k = BOARD_ROW_SIZE - 2;
	int toDelete;
	char temp[BOARD_ROW_SIZE][BOARD_COL_SIZE] = { EMPTY, };
	for (i = BOARD_ROW_SIZE - 2; i > 0; i--){
		toDelete = False;
		for (j = 0; j < BOARD_COL_SIZE; j++){
			if (i == indexes[j]){
				toDelete = True;
				break;
			}
		}
		if (!toDelete){
			for (j = 0; j < BOARD_COL_SIZE; j++){
				temp[k][j] = tetrisManager->board[i][j];
			}
			k--;
		}
	}
	for (i = 1; i < BOARD_ROW_SIZE - 1; i++){
		for (j = 1; j < BOARD_COL_SIZE - 1; j++){
			tetrisManager->board[i][j] = temp[i][j];
		}
	}
	for (i = 0; i < count; i++){
		tetrisManager->shadow = Block_Move(tetrisManager->shadow, DOWN); // lower shadow block by deleted count
		tetrisManager->score += tetrisManager->speedLevel * 100;
		tetrisManager->deletedLineCount++;
		if (tetrisManager->deletedLineCount % LEVELP_UP_CONDITION == 0){
			_TetrisManager_UpSpeedLevel(tetrisManager);
		}
	}
}

static void _TetrisManager_HighlightLinesToDelete(TetrisManager* tetrisManager, int* indexes, int count){
	int i;
	int j;
	int k;
	for (i = 0; i < LINES_TO_DELETE_HIGHTING_COUNT; i++){
		FontUtil_ChangeFontColor(JADE);
		Sleep(LINES_TO_DELETE_HIGHTING_MILLISECOND);
		for (j = 0; j < count; j++){

			WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
			CursorUtil_GotoXY(2, indexes[j]);
			for (k = 0; k < BOARD_COL_SIZE - 2; k++){
				printf("▧");
			}
			ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
		}
		FontUtil_ChangeFontColor(DEFAULT_FONT_COLOR);
		Sleep(LINES_TO_DELETE_HIGHTING_MILLISECOND);
		for (j = 0; j < count; j++){

			WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
			CursorUtil_GotoXY(2, indexes[j]);
			for (k = 0; k < BOARD_COL_SIZE - 2; k++){
				printf("  ");
			}
			ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
		}
	}
}

static Block _TetrisManager_GetBlockByType(TetrisManager* tetrisManager, int blockType){
	if (blockType == SHADOW_BLOCK){
		return tetrisManager->shadow;
	}
	else{
		return tetrisManager->block;
	}
}

static void _TetrisManager_MakeShadow(TetrisManager* tetrisManager){
	tetrisManager->shadow = tetrisManager->block;
	while (!TetrisManager_IsReachedToBottom(tetrisManager, SHADOW_BLOCK)){
		_TetrisManager_ChangeBoardByDirection(tetrisManager, SHADOW_BLOCK, DOWN);
	}
	_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, SHADOW_BLOCK);
}

static int _TetrisManager_CheckValidPosition(TetrisManager* tetrisManager, int blockType, int direction){
	Block temp = Block_Move(_TetrisManager_GetBlockByType(tetrisManager, blockType), direction);
	int i;
	for (i = 0; i < POSITIONS_SIZE; i++){
		int x = Block_GetPositions(temp)[i].x;
		int y = Block_GetPositions(temp)[i].y;

		//but now, x == 0 is empty
		//originally, x == 0 is top wall
		//because we convert the center top wall into empty intentionally
		if (blockType == MOVING_BLOCK && x == 0){
			return TOP_WALL;
		}
		if (!(tetrisManager->board[x][y] == EMPTY || tetrisManager->board[x][y] == MOVING_BLOCK || tetrisManager->board[x][y] == SHADOW_BLOCK)){
			return tetrisManager->board[x][y];
		}
	}
	return EMPTY;
}

static void _TetrisManager_ChangeBoardByDirection(TetrisManager* tetrisManager, int blockType, int direction){
	int tempDirection = DOWN;
	int tempCheckResult = EMPTY;
	int checkResult;
	_TetrisManager_ChangeBoardByStatus(tetrisManager, blockType, EMPTY);
	checkResult = _TetrisManager_CheckValidPosition(tetrisManager, blockType, direction);
	if (checkResult == EMPTY){
		if (blockType == MOVING_BLOCK){
			tetrisManager->block = Block_Move(tetrisManager->block, direction);
		}
		else if (blockType == SHADOW_BLOCK){
			tetrisManager->shadow = Block_Move(tetrisManager->shadow, direction);
		}
	}
	else{
		if ((direction == UP || direction == LEFT || direction == RIGHT) && checkResult != FIXED_BLOCK){
			if (checkResult == TOP_WALL){
				tempDirection = DOWN;
				tempCheckResult = TOP_WALL;
			}
			else if (checkResult == RIGHT_WALL){
				tempDirection = LEFT;
				tempCheckResult = RIGHT_WALL;
			}
			else if (checkResult == LEFT_WALL){
				tempDirection = RIGHT;
				tempCheckResult = LEFT_WALL;
			}
			do{
				tetrisManager->block = Block_Move(tetrisManager->block, tempDirection);
			} while (_TetrisManager_CheckValidPosition(tetrisManager, MOVING_BLOCK, direction) == tempCheckResult);
			tetrisManager->block = Block_Move(tetrisManager->block, direction);
		}
	}
	_TetrisManager_ChangeBoardByStatus(tetrisManager, blockType, blockType);
}

static void _TetrisManager_ChangeBoardByStatus(TetrisManager* tetrisManager, int blockType, int status){
	int i;
	Block block = _TetrisManager_GetBlockByType(tetrisManager, blockType);
	for (i = 0; i < POSITIONS_SIZE; i++){
		int x = Block_GetPositions(block)[i].x;
		int y = Block_GetPositions(block)[i].y;
		tetrisManager->board[x][y] = status;
	}
}

static DWORD WINAPI _TetrisManager_OnTotalTimeThreadStarted(void *tetrisManager){
	int i;
	int x;
	int y;
	int interval=((TetrisManager*)tetrisManager)->blindNextInterval;					//레벨별로 다르게 설정해둔 주기

	while (True){
		if (!((TetrisManager*)tetrisManager)->isTotalTimeAvailable){					//현재 상태가 pause나 stop상태인지를 체크
			break;
		}
		Sleep(MILLI_SECONDS_PER_SECOND);
		((TetrisManager*)tetrisManager)->totalTime++;

		if((((TetrisManager*)tetrisManager)->totalTime)%interval==0){
			
			WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
			TetrisManager_BlindNextBlock((TetrisManager*)tetrisManager);				// 다음블록을 숨기는 함수를 실행
			((TetrisManager*)tetrisManager)->checkBlindStatus=True;						// 다음블록이 숨겨지고 있는 상태로 표시
			ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제

			if (!((TetrisManager*)tetrisManager)->isTotalTimeAvailable){					// 현재 상태가 pause나 stop상태인지를 다시 체크
				break;
			}
			for(i=0; i<interval;i++){
				Sleep(MILLI_SECONDS_PER_SECOND);
				((TetrisManager*)tetrisManager)->totalTime++;							// totalTime을 1초에 한 번씩 증가시킴
			}

			if (!((TetrisManager*)tetrisManager)->isTotalTimeAvailable){					// 현재 상태가 pause나 stop상태인지를 다시 체크
				break;
			}
			WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
			x=40;																		// use temp size (magic number)
			y=15;
			Block_PrintNext(((TetrisManager*)tetrisManager)->block, 0, x, y);			// 다음 블럭 출력			
			x+=20;
			Block_PrintNext(((TetrisManager*)tetrisManager)->block, 1, x, y);			// 다다음 블럭 출력		
			((TetrisManager*)tetrisManager)->checkBlindStatus=False;						// 다음블록이 숨겨지고 있지않는 상태로 표시
			ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
		}		
	}
	return 0;
}

static void _TetrisManager_PrintTotalTime(TetrisManager tetrisManager){
	int hour = tetrisManager.totalTime / (60 * 60);
	int minute = tetrisManager.totalTime % (60 * 60) / 60;
	int second = tetrisManager.totalTime % 60;

	// use temp size (magic number)
	int x = 42;
	int y = 20;

	WaitForSingleObject(tetrisManager.mutex,INFINITE);		// LOCK 걸기
	CursorUtil_GotoXY(x, y++);
	printf("┏  time  ┓");
	CursorUtil_GotoXY(x, y++);
	printf("┃%02d:%02d:%02d┃", hour, minute, second);
	CursorUtil_GotoXY(x, y++);
	printf("┗━━━━┛");
	ReleaseMutex(tetrisManager.mutex);						// LOCK 해제
}

static void _TetrisManager_MakeObstacleOneLine(TetrisManager* tetrisManager){
	int i;
	int j;
	int isFixedBlock;
	int fixedBlockCount = 0;
	char temp[BOARD_ROW_SIZE][BOARD_COL_SIZE] = { EMPTY, };
	for (i = 1; i < BOARD_COL_SIZE - 1; i++){
		if (tetrisManager->board[1][i] == FIXED_BLOCK){
			return;
		}
	}
	srand((unsigned int)time(NULL));
	for (i = 1; i < BOARD_COL_SIZE - 1; i++){
		isFixedBlock = rand() % 2;
		fixedBlockCount += isFixedBlock;
		temp[BOARD_ROW_SIZE - 2][i] = isFixedBlock ? FIXED_BLOCK : EMPTY;
	}
	if (fixedBlockCount == BOARD_COL_SIZE - 2){
		temp[BOARD_ROW_SIZE - 2][rand() % (BOARD_COL_SIZE - 2) + 1] = EMPTY;
	}
	for (i = BOARD_ROW_SIZE - 2; i > 0; i--){
		for (j = 1; j < BOARD_COL_SIZE - 1; j++){
			temp[i - 1][j] = tetrisManager->board[i][j];
		}
	}
	for (i = 1; i < BOARD_ROW_SIZE - 1; i++){
		for (j = 1; j < BOARD_COL_SIZE - 1; j++){
			tetrisManager->board[i][j] = temp[i][j];
		}
	}
	_TetrisManager_MakeShadow(tetrisManager);
	TetrisManager_PrintBoard(tetrisManager);
}

void TetrisManager_Item_RemoveOneRow(TetrisManager* tetrisManager){
	//아이템1 : 한 줄 제거
	int indexes[1]={BOARD_ROW_SIZE-2};											//제거할 행을 담는 index배열에 블록이 존재하는 맨 아래 행의 번호를 담는다.
	int count=1;																//index의 길이를 저장한다.

	// use temp size (magic number)
	int x = 38;
	int y = 1;

	_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, EMPTY);				//삭제할 행 깜빡이는 동안 그림자 블록 안보이게 한다.
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);				//삭제할 행 깜빡이는 동안 내려오는 블록 안보이게 한다.
	_TetrisManager_HighlightLinesToDelete(tetrisManager, indexes, count);		//삭제할 행을 깜빡인다.
	_TetrisManager_Item_DeleteLines(tetrisManager, indexes, count);				//삭제 후 다시 그려질 상태를 tetrisManager->board에 담는다.

	TetrisManager_PrintBoard(tetrisManager);									//변경된 행에 맞게 tetrisManager->board에 담겨진 대로 board에 블럭을 다시 그린다.

	TetrisManager_Item_ProcessBLOCK(tetrisManager,SHADOW_BLOCK,1);				//그림자 블럭은 한 칸 위로 유지한다.
	TetrisManager_MaintainBoard(tetrisManager);									//board의 테두리가 망가지지 않게 유지한다.

	_TetrisManager_PrintStatus(tetrisManager, x, y);							//레벨, 제거 라인수, 점수 표시를 업데이트 한다.
	
}

static void _TetrisManager_Item_DeleteLines(TetrisManager* tetrisManager, int* indexes, int count){
	// #define BOARD_ROW_SIZE 24
	// #define BOARD_COL_SIZE 14
	
	int i;
	int j;
	int k = BOARD_ROW_SIZE - 2;		
	int toDelete;
	char temp[BOARD_ROW_SIZE][BOARD_COL_SIZE] = { EMPTY, };

	for (i = BOARD_ROW_SIZE - 2; i > 0; i--){				//총 board의 행에서 블록이 담겨지는 행들만큼 for문으로 돌면서 검사한다.
		toDelete = False;
		for (j = 0; j < count; j++){						
			if (i == indexes[j]){							//검사하는 행번호와 index배열의 값이 동일하면 toDelete=true
				toDelete = True;
				break;
			}
		}
		if (!toDelete){										//index배열에 들어있지 않던 값의 행번호들이면,
			for (j = 0; j < BOARD_COL_SIZE; j++){
				temp[k][j] = tetrisManager->board[i][j]; 	//temp : 삭제할 행들을 제외하고 남는 행들을 다시 차곡차곡 쌓는다.
			}
			k--;
		}
	}
	
	for (i = 1; i < BOARD_ROW_SIZE - 1; i++){				//temp를 다시 board에 담는다.
		for (j = 1; j < BOARD_COL_SIZE - 1; j++){
			tetrisManager->board[i][j] = temp[i][j];
		}
	}

	TetrisManager_MaintainBoard(tetrisManager);				//board의 테두리를 유지한다.
}

static void TetrisManager_Item_ProcessBLOCK(TetrisManager* tetrisManager, int blockType, int number){	
	//아이템을 통해 블록 제거 후 shadow블록이나 moving블록의 위치를 재설정해준다.
	int i;
	Block block = _TetrisManager_GetBlockByType(tetrisManager, blockType);

	for (i = 0; i < POSITIONS_SIZE; i++){
		int x = Block_GetPositions(block)[i].x;
		int y = Block_GetPositions(block)[i].y;
		
		WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
		CursorUtil_GotoXY(2*y, (x+number));
		
		if((x+number)!=BOARD_ROW_SIZE - 1){
			printf("%s", boardTypesToPrint[(int)EMPTY]);	
		}
		ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
	}

	FontUtil_ChangeFontColor(DEFAULT_FONT_COLOR);
	
	_TetrisManager_PrintTotalTime(*tetrisManager); // because of multi thread problem, this function covers total time
}

static void TetrisManager_MaintainBoard(TetrisManager* tetrisManager){
	//아이템을 통해 블록 제거 후 board의 테두리 모양이 망가지지 않게 다시 설정해준다.
	int i;

	for (i = 0; i < BOARD_ROW_SIZE; i++){
		tetrisManager->board[i][0] = LEFT_WALL;						//board의 왼쪽 부분
		tetrisManager->board[i][BOARD_COL_SIZE - 1] = RIGHT_WALL;	//board의 오른쪽 부분
	}
	for (i = 0; i < BOARD_COL_SIZE; i++){
		tetrisManager->board[0][i] = TOP_WALL;						//board의 가장 윗 부분
		tetrisManager->board[BOARD_ROW_SIZE - 1][i] = BOTTOM_WALL;	//board의 바닥 부분
	}

	//in order to make center hole at top wall, we convert center top wall into empty intentionally
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2 - 1] = EMPTY;	//윗 부분에 블럭이 나오는 구멍을 표현한다.
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2] = EMPTY;
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2 + 1] = EMPTY;
	tetrisManager->board[0][(BOARD_COL_SIZE - 2) / 2 + 2] = EMPTY;

	tetrisManager->board[0][0] = LEFT_TOP_EDGE;						//각 모서리는 -모양이 아닌 ㄴ,ㄱ과 같이 꺾인 모양으로 그려준다.
	tetrisManager->board[0][BOARD_COL_SIZE - 1] = RIGHT_TOP_EDGE;
	tetrisManager->board[BOARD_ROW_SIZE - 1][0] = LEFT_BOTTOM_EDGE;
	tetrisManager->board[BOARD_ROW_SIZE - 1][BOARD_COL_SIZE - 1] = RIGHT_BOTTOM_EDGE;
}

void TetrisManager_Item_RemoveTwoRow(TetrisManager* tetrisManager){
	//아이템2 : 두 줄 제거
	int indexes[2]={BOARD_ROW_SIZE-2,BOARD_ROW_SIZE-3};							//제거할 행을 담는 index배열에 맨 아래부터 두개의 행의 번호를 담는다.
	int count=2;

	// use temp size (magic number)
	int x = 38;
	int y = 1;

	_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, EMPTY);				//삭제할 행 깜빡이는 동안 그림자 블록 안보이게	
	_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);				//삭제할 행 깜빡이는 동안 내려오는 블록 안보이게
	_TetrisManager_HighlightLinesToDelete(tetrisManager, indexes, count);		//삭제할 행을 깜빡인다.
	_TetrisManager_Item_DeleteLines(tetrisManager, indexes, count);				//삭제 후 다시 그려질 상태를 tetrisManager->board에 담음.	

	TetrisManager_PrintBoard(tetrisManager);									//변경된 행에 맞게 tetrisManager->board에 담겨진 대로 board에 블럭을 다시 그린다.

	TetrisManager_Item_ProcessBLOCK(tetrisManager,MOVING_BLOCK,2);				//움직이던 블럭과 그림자 블럭은 두칸 위로 올려줌으로써 기존의 자리를 지킨다.
	TetrisManager_Item_ProcessBLOCK(tetrisManager,SHADOW_BLOCK,2);				
	TetrisManager_MaintainBoard(tetrisManager);									//테두리 유지

	_TetrisManager_PrintStatus(tetrisManager, x, y);							//레벨, 제거 라인수, 점수 표시
}

void TetrisManager_Item_RemoveAllRow(TetrisManager* tetrisManager){
	//아이템3 : 전체 줄 제거
	int indexes[BOARD_ROW_SIZE];
	int count;

	// use temp size (magic number)
	int x = 38;
	int y = 1;

	TetrisManager_SearchAllLineIndexesToDelete(tetrisManager, indexes, &count);		//삭제해야 할 행의 번호들을 indexes에 담는다.
	
	if (count > 0){

		_TetrisManager_PrintBlock(tetrisManager, SHADOW_BLOCK, EMPTY);				//삭제할 행 깜빡이는 동안 그림자 블록 안보이게
		_TetrisManager_PrintBlock(tetrisManager, MOVING_BLOCK, EMPTY);				//삭제할 행 깜빡이는 동안 내려오는 블록 안보이게
		_TetrisManager_HighlightLinesToDelete(tetrisManager, indexes, count);		//삭제할 행을 깜빡인다.
		_TetrisManager_Item_DeleteLines(tetrisManager, indexes, count);				//삭제

		_TetrisManager_PrintStatus(tetrisManager, x, y);							//레벨, 제거 라인수, 점수 표시	
	}
}

static void TetrisManager_SearchAllLineIndexesToDelete(TetrisManager* tetrisManager, int* indexes, int* count){
	int i;
	int j;
	memset(indexes, -1, sizeof(int)* (BOARD_ROW_SIZE - 2));		//indexes을 초기화
	*count = 0;
	for (i = 1; i < BOARD_ROW_SIZE - 1; i++){					//행 전체를 indexes에 담고 행 갯수를 세어 count에 담는다.
		for (j = 1; j < BOARD_COL_SIZE - 1; j++){
			if (tetrisManager->board[i][j] == FIXED_BLOCK){
				indexes[(*count)++] = i;
				break;
			}
		}
	}
}

void TetrisManager_ChangeNextBlock(TetrisManager* tetrisManager){
	// use temp size (magic number)
	int x = 40;
	int y = 15;

	if((tetrisManager->changeNextCount>0)&&(tetrisManager->checkBlindStatus==False)){
		//다음블록과 다다음블록을 바꿀 수 있는 횟수가 남아있고, 다음블럭이 숨겨져있는 상태가 아니라면

		Block_ChangeNext(tetrisManager->block);										// queue에서 구조적으로 변경

		//view에서도 변경된 모습대로 보이도록 다시 print
		WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
		Block_PrintNext(tetrisManager->block, 0, x, y);								// 다음 블럭 출력
		ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제

		x += 20;

		WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기
		Block_PrintNext(tetrisManager->block, 1, x, y);								// 다다음 블럭 출력
		ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제

		tetrisManager->changeNextCount--;											// 다음블록과 다다음블록을 바꿀 수 있는 횟수를 1회 차감
	}
	printf("%d",tetrisManager->changeNextCount);										// 다음블록과 다다음블록을 바꿀 수 있는 횟수를 출력
}

static void TetrisManager_InitializeNextCount(TetrisManager* tetrisManager,int speedLevel){

	switch(speedLevel){
		case 1:
			tetrisManager->changeNextCount=10;
			tetrisManager->blindNextInterval=5;
			break;
		case 2:
			tetrisManager->changeNextCount=9;
			tetrisManager->blindNextInterval=5;
			break;
		case 3:
			tetrisManager->changeNextCount=8;
			tetrisManager->blindNextInterval=4;
			break;
		case 4:
			tetrisManager->changeNextCount=7;
			tetrisManager->blindNextInterval=4;
			break;
		case 5:
			tetrisManager->changeNextCount=6;
			tetrisManager->blindNextInterval=3;
			break;
		case 6:
			tetrisManager->changeNextCount=5;
			tetrisManager->blindNextInterval=3;
			break;
		case 7:
			tetrisManager->changeNextCount=4;
			tetrisManager->blindNextInterval=2;
			break;
		case 8:
			tetrisManager->changeNextCount=3;
			tetrisManager->blindNextInterval=2;
			break;
		case 9:
			tetrisManager->changeNextCount=2;
			tetrisManager->blindNextInterval=1;
			break;
		case 10:
			tetrisManager->changeNextCount=1;
			tetrisManager->blindNextInterval=1;
			break;
	}
	printf("%d",tetrisManager->changeNextCount);
}

void TetrisManager_BlindNextBlock(TetrisManager* tetrisManager){
	int x,y;

	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기 
	x=40;																		// use temp size (magic number)
	y=15;
	Block_BlindNext(tetrisManager->block, 0, x, y);								// 다음 블럭 숨기기	
	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제

	WaitForSingleObject(((TetrisManager*)tetrisManager)->mutex,INFINITE);		// LOCK 걸기 
	x+=20;
	Block_BlindNext(tetrisManager->block, 1, x, y);								// 다다음 블럭 숨기기
	ReleaseMutex(((TetrisManager*)tetrisManager)->mutex);						// LOCK 해제
}