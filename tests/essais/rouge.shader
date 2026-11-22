shader_type canvas_item;

uniform float force : hint_range(0.0, 1.0) = 1.0;
uniform vec4 teinte : source_color = vec4(1.0, 0.0, 0.0, 1.0);

void fragment() {
	COLOR.rgb = mix(COLOR.rgb, teinte.rgb, force);
}
