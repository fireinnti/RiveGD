extends RiveCanvas

var svg: RiveSVG
var instances: Array[RiveSVG] = []
var rows = 9
var cols = 16

func _ready():
	svg = RiveSVG.new()
	svg.load_file("res://23.svg")
	
	# Create instances to test Rive instancing performance
	for i in range(rows * cols):
		instances.append(svg.instance())
	
	draw_rive.connect(_on_draw_rive)

func _process(delta):
	$Label.text = str(Engine.get_frames_per_second()) + "FPS"

func _on_draw_rive(renderer: RiveRendererWrapper):
	var spacing_x = 60.0
	var spacing_y = 60.0
	
	var idx = 0
	for y in range(rows):
		for x in range(cols):
			if idx >= instances.size(): break
			
			renderer.save()
			renderer.translate(20 + x * spacing_x, 20 + y * spacing_y)
			renderer.scale(0.5, 0.5)
			
			# Draw the specific instance
			instances[idx].draw(renderer)
			
			renderer.restore()
			idx += 1
