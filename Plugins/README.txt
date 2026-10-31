GeSe v2.4.0 - Plugins
======================

Put your own effects (.gsfx files) into this folder or into
%APPDATA%\GeSe\Plugins (File > Import > Plugin Effect does that for you).

A .gsfx file is plain text: a header that declares the effect and its
parameters, followed by an HLSL function:

  @effect My Effect
  @category Plugins
  @param float amount "Amount" 50 0 100 %
  @code
  float4 effect(float2 uv) { float4 c = SRC(uv); c.rgb *= 1.0 + amount / 100.0; return c; }
  @end

Every effect automatically gets an Intensity parameter (and Speed with @speed).
All parameters can be keyframed in the Timeline or in Effect Controls.

Full reference: GeSe > Help > Plugin SDK.
Manage, disable or reload plugins: Window > Plugin Manager.
