#ifndef _CITStateRideCart_
#define _CITStateRideCart_
#include "citstate.h"

//----------------------------------------------------------------------------------
/// @brief 2ﾀﾎｽﾂ ﾄｫﾆｮ ｺｸﾁｶｼｮｿ｡ ﾅﾀﾀｻｶｧﾀﾇ ﾀﾎﾅﾍﾆ菎ﾌｽｺ ｻﾂ
//----------------------------------------------------------------------------------
class CITStateRideCart : public CITState
{
public:
	CITStateRideCart(void);
	virtual ~CITStateRideCart(void);

	virtual void Enter();
	virtual void Leave();
	virtual unsigned Process( unsigned uiMsg, WPARAM wParam, LPARAM lParam );
	virtual void Update( POINT ptMouse ){}

};
#endif