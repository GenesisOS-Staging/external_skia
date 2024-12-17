diagnostic(off, derivative_uniformity);
diagnostic(off, chromium.unreachable_code);
struct FSOut {
  @location(0) sk_FragColor: vec4<f32>,
};
fn _skslMain(_stageOut: ptr<function, FSOut>) {
  {
    const a: mat3x3<f32> = mat3x3<f32>(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    const _skTemp0 = mat2x2<f32>(1.0, 0.0, 0.0, 1.0);
    const b: mat3x3<f32> = mat3x3<f32>(_skTemp0[0][0], _skTemp0[0][1], 0.0, _skTemp0[1][0], _skTemp0[1][1], 0.0, 0.0, 0.0, 1.0);
    (*_stageOut).sk_FragColor.x = f32(select(1, 0, all(a[0] == b[0])));
  }
}
@fragment fn main() -> FSOut {
  var _stageOut: FSOut;
  _skslMain(&_stageOut);
  return _stageOut;
}
