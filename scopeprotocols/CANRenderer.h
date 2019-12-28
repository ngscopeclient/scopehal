/**
  @file
  @author Andrés MANELLI
  @brief Declaration of CANRenderer
  */

#ifndef CANRenderer_h
#define CANRenderer_h

#include "../scopehal/TextRenderer.h"

/**
  @brief Renderer for a CAN channel
  */
class CANRenderer : public TextRenderer
{
public:
	CANRenderer(OscilloscopeChannel* channel);

protected:
	virtual std::string GetText(int i);
	virtual Gdk::Color GetColor(int i);
};

#endif
