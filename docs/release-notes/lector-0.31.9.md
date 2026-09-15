## Fixed

- The four buttons along the bottom of the screen now answer every tap. They
  worked only some of the time on the X4 Pro: a tap is a single one-shot event,
  and whichever part of the screen asked for it first could eat it before the
  button did. The tap is now read once per input pass and shared, so the button
  under your finger always gets it.
