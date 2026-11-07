model DCDrive
  parameter Modelica.Units.SI.Resistance R = 1 "Armature resistance";
  parameter Modelica.Units.SI.Inductance L = 0.05 "Armature inductance";
  parameter Real k(unit = "N.m/A") = 0.5 "Motor constant";
  parameter Modelica.Units.SI.Inertia J_motor = 0.01 "Rotor inertia";
  parameter Modelica.Units.SI.Inertia J_load = 0.05 "Load inertia";
  parameter Modelica.Units.SI.RotationalSpringConstant c = 10 "Shaft stiffness";
  parameter Modelica.Units.SI.RotationalDampingConstant d = 1 "Shaft damping";
  Modelica.Electrical.Analog.Sources.SignalVoltage V1 annotation(
    Placement(visible = true, transformation(origin = {100, -80}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Electrical.Analog.Basic.Resistor R1(R = R) annotation(
    Placement(visible = true, transformation(origin = {170, -80}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Electrical.Analog.Basic.Inductor L1(L = L, i(start = 0, fixed = true)) annotation(
    Placement(visible = true, transformation(origin = {240, -80}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Electrical.Analog.Basic.RotationalEMF R2(k = k) annotation(
    Placement(visible = true, transformation(origin = {310, -80}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Electrical.Analog.Basic.Ground G1 annotation(
    Placement(visible = true, transformation(origin = {380, -80}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Mechanics.Rotational.Components.Inertia I1(J = J_motor) annotation(
    Placement(visible = true, transformation(origin = {450, -80}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Mechanics.Rotational.Components.SpringDamper S1(c = c, d = d) annotation(
    Placement(visible = true, transformation(origin = {100, -140}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Mechanics.Rotational.Components.Inertia I2(J = J_load, phi(start = 0, fixed = true), w(start = 0, fixed = true)) annotation(
    Placement(visible = true, transformation(origin = {170, -140}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Mechanics.Rotational.Sensors.SpeedSensor S2 annotation(
    Placement(visible = true, transformation(origin = {240, -140}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Mechanics.Rotational.Sensors.AngleSensor A1 annotation(
    Placement(visible = true, transformation(origin = {310, -140}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Blocks.Interfaces.RealInput v(unit = "V") "Armature voltage" annotation(
    Placement(visible = true, transformation(origin = {0, -80}, extent = {{-10, -10}, {10, 10}}), iconTransformation(extent = {{-210, -10}, {-190, 10}})));
  Modelica.Blocks.Interfaces.RealOutput w(unit = "rad/s") "Load speed" annotation(
    Placement(visible = true, transformation(origin = {340, -140}, extent = {{-10, -10}, {10, 10}}), iconTransformation(extent = {{190, 20}, {210, 40}})));
  Modelica.Blocks.Interfaces.RealOutput phi(unit = "rad") "Load angle" annotation(
    Placement(visible = true, transformation(origin = {410, -140}, extent = {{-10, -10}, {10, 10}}), iconTransformation(extent = {{190, -40}, {210, -20}})));
  Real i(unit = "A") = L1.i "Armature current" annotation(
    __paperboat(probe = true));
equation
  connect(R1.p, V1.p);
  connect(L1.p, R1.n);
  connect(R2.p, L1.n);
  connect(V1.n, R2.n);
  connect(G1.p, V1.n);
  connect(I1.flange_a, R2.flange);
  connect(S1.flange_a, I1.flange_b);
  connect(I2.flange_a, S1.flange_b);
  connect(S2.flange, I2.flange_b);
  connect(A1.flange, I2.flange_b);
  connect(v, V1.v);
  connect(S2.w, w);
  connect(A1.phi, phi);
  annotation(
    Icon(coordinateSystem(extent = {{-200, -100}, {200, 100}}), graphics = {Ellipse(extent = {{-180, -40}, {-20, 40}}, lineColor = {0, 0, 0}, fillColor = {255, 255, 255}, pattern = LinePattern.Solid, fillPattern = FillPattern.Solid, lineThickness = 0.25), Line(points = {{-40, 0}, {200, 0}}, color = {0, 0, 0}, pattern = LinePattern.Solid, thickness = 0.5), Ellipse(extent = {{40, -40}, {200, 40}}, lineColor = {0, 0, 0}, fillColor = {255, 255, 255}, pattern = LinePattern.Solid, fillPattern = FillPattern.Solid, lineThickness = 0.5), Text(extent = {{-260, -20}, {60, 20}}, textString = "M", fontSize = 12, textColor = {0, 0, 0}, fontName = "Courier New")}));
end DCDrive;