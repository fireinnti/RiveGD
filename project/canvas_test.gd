extends RiveCanvas

var path: RivePath
var paint: RivePaint
var time: float = 0.0

func _ready():
	path = RivePath.new()
	paint = RivePaint.new()
	
	paint.set_color(Color(1, 0, 0, 1))
	paint.set_thickness(10.0)
	paint.set_style(1)
	
	draw_rive.connect(_on_draw_rive)

func _process(delta):
	time += delta
	
	path.reset()
	var center = get_size() / 2.0
	var radius = 100.0 + sin(time * 2.0) * 50.0
	
	path.move_to(center.x + radius, center.y)
	for i in range(1, 32):
		var angle = i * TAU / 32.0
		var x = center.x + cos(angle) * radius
		var y = center.y + sin(angle) * radius
		path.line_to(x, y)
	path.close()


func _on_draw_rive(renderer: RiveRendererWrapper):
	renderer.save()
	renderer.draw_path(path, paint)
	renderer.restore()
