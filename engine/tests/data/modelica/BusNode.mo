model BusNode "Local fixture with vector port Pin p[n] and conditional heatPort"
  parameter Integer n = 3;
  parameter Boolean useHeatPort = false;
  Modelica.Electrical.Analog.Interfaces.PositivePin p[n]
    annotation(Placement(iconTransformation(extent = {{-110, -10}, {-90, 10}})));
  Modelica.Electrical.Analog.Interfaces.NegativePin q
    annotation(Placement(iconTransformation(extent = {{90, -10}, {110, 10}})));
  Modelica.Thermal.HeatTransfer.Interfaces.HeatPort_a heatPort if useHeatPort
    annotation(Placement(iconTransformation(extent = {{-10, -110}, {10, -90}})));
  annotation(Icon(coordinateSystem(extent = {{-100, -100}, {100, 100}}),
                  graphics = {
                    Rectangle(extent = {{-60, 40}, {60, -40}},
                              lineColor = {0, 0, 0},
                              fillColor = {255, 255, 255},
                              fillPattern = FillPattern.Solid),
                    Text(extent = {{-60, 20}, {60, -20}},
                         textString = "n=%n")
                  }));
end BusNode;
