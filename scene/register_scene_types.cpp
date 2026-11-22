#include "register_scene_types.h"

#include "core/io/resource_loader.h"
#include "scene/2d/animated_sprite_2d.h"
#include "scene/2d/camera_2d.h"
#include "scene/2d/collision_shape_2d.h"
#include "scene/2d/node_2d.h"
#include "scene/2d/physics_body_2d.h"
#include "scene/2d/ray_cast_2d.h"
#include "scene/2d/cpu_particles_2d.h"
#include "scene/2d/light_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/2d/tile_map.h"
#include "scene/3d/collision_shape_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics_body_3d.h"
#include "scene/3d/visual_3d.h"
<<<<<<< HEAD
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/resources/gltf_loader.h"
=======
#include "scene/audio/audio_stream_player.h"
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
#include "scene/animation/animation_player.h"
#include "scene/gui/button.h"
#include "scene/gui/container.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "scene/gui/panel.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/widgets.h"
#include "scene/resources/theme.h"
#include "scene/main/canvas_item.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/timer.h"
#include "scene/resources/font.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/shape_2d.h"
#include "scene/resources/shape_3d.h"
#include "scene/resources/texture.h"

void register_scene_types() {
	// Arbre de scène
	GDREGISTER_CLASS(Node);
	GDREGISTER_ABSTRACT_CLASS(SceneTree);
	GDREGISTER_ABSTRACT_CLASS(SceneTreeTimer);
	GDREGISTER_ABSTRACT_CLASS(CanvasItem);
	GDREGISTER_CLASS(CanvasLayer);
	GDREGISTER_CLASS(Timer);
	GDREGISTER_CLASS(AnimationPlayer);

	// Ressources
	GDREGISTER_CLASS(Texture2D);
	GDREGISTER_CLASS(Font);
	GDREGISTER_CLASS(PackedScene);
	GDREGISTER_ABSTRACT_CLASS(Shape2D);
	GDREGISTER_CLASS(RectangleShape2D);
	GDREGISTER_CLASS(CircleShape2D);
	GDREGISTER_CLASS(CapsuleShape2D);
	GDREGISTER_CLASS(ConvexPolygonShape2D);
	GDREGISTER_CLASS(Theme);
	GDREGISTER_ABSTRACT_CLASS(Tweener);
	GDREGISTER_CLASS(PropertyTweener);
	GDREGISTER_CLASS(IntervalTweener);
	GDREGISTER_CLASS(CallbackTweener);
	GDREGISTER_CLASS(MethodTweener);
	GDREGISTER_CLASS(Tween);
	GDREGISTER_CLASS(Shader);
	GDREGISTER_CLASS(CanvasItemMaterial);
	GDREGISTER_CLASS(ShaderMaterial);
	GDREGISTER_ABSTRACT_CLASS(AudioEffect);
	GDREGISTER_CLASS(AudioEffectAmplify);
	GDREGISTER_ABSTRACT_CLASS(AudioEffectFilter);
	GDREGISTER_CLASS(AudioEffectLowPassFilter);
	GDREGISTER_CLASS(AudioEffectHighPassFilter);
	GDREGISTER_CLASS(AudioEffectDelay);
	GDREGISTER_CLASS(AudioEffectReverb);
	GDREGISTER_CLASS(AudioEffectDistortion);
	GDREGISTER_CLASS(AudioEffectLimiter);
	GDREGISTER_ABSTRACT_CLASS(AudioStream);
	GDREGISTER_CLASS(AudioStreamWAV);
	GDREGISTER_CLASS(AudioStreamOggVorbis);
	GDREGISTER_CLASS(AudioStreamMP3);
	GDREGISTER_ABSTRACT_CLASS(Shape3D);
	GDREGISTER_CLASS(SphereShape3D);
	GDREGISTER_CLASS(BoxShape3D);
	GDREGISTER_CLASS(CapsuleShape3D);
	GDREGISTER_ABSTRACT_CLASS(Mesh);
	GDREGISTER_ABSTRACT_CLASS(PrimitiveMesh);
	GDREGISTER_CLASS(BoxMesh);
	GDREGISTER_CLASS(SphereMesh);
	GDREGISTER_CLASS(CylinderMesh);
	GDREGISTER_CLASS(CapsuleMesh);
	GDREGISTER_CLASS(PlaneMesh);
	GDREGISTER_CLASS(TorusMesh);
	GDREGISTER_CLASS(ArrayMesh);
	GDREGISTER_ABSTRACT_CLASS(Material);
	GDREGISTER_CLASS(StandardMaterial3D);
	GDREGISTER_CLASS(Environment);

	// 2D
	GDREGISTER_CLASS(Node2D);
	GDREGISTER_CLASS(Marker2D);
	GDREGISTER_CLASS(Sprite2D);
	GDREGISTER_CLASS(AnimatedSprite2D);
	GDREGISTER_CLASS(CPUParticles2D);
	GDREGISTER_CLASS(CanvasModulate);
	GDREGISTER_CLASS(PointLight2D);
	GDREGISTER_CLASS(LightOccluder2D);
	GDREGISTER_CLASS(Camera2D);
	GDREGISTER_CLASS(TileMap);
	GDREGISTER_ABSTRACT_CLASS(CollisionObject2D);
	GDREGISTER_ABSTRACT_CLASS(PhysicsBody2D);
	GDREGISTER_CLASS(StaticBody2D);
	GDREGISTER_CLASS(CharacterBody2D);
	GDREGISTER_CLASS(RigidBody2D);
	GDREGISTER_CLASS(Area2D);
	GDREGISTER_CLASS(CollisionShape2D);
	GDREGISTER_CLASS(RayCast2D);

	// 3D
	GDREGISTER_CLASS(Node3D);
	GDREGISTER_CLASS(Marker3D);
	GDREGISTER_CLASS(MeshInstance3D);
	GDREGISTER_CLASS(Camera3D);
	GDREGISTER_ABSTRACT_CLASS(Light3D);
	GDREGISTER_CLASS(DirectionalLight3D);
	GDREGISTER_CLASS(OmniLight3D);
	GDREGISTER_CLASS(SpotLight3D);
	GDREGISTER_CLASS(WorldEnvironment);
	GDREGISTER_ABSTRACT_CLASS(CollisionObject3D);
	GDREGISTER_ABSTRACT_CLASS(PhysicsBody3D);
	GDREGISTER_CLASS(StaticBody3D);
	GDREGISTER_CLASS(CharacterBody3D);
	GDREGISTER_CLASS(RigidBody3D);
	GDREGISTER_CLASS(Area3D);
	GDREGISTER_CLASS(CollisionShape3D);
	GDREGISTER_CLASS(RayCast3D);

	// Interface
	GDREGISTER_CLASS(Control);
	GDREGISTER_CLASS(Label);
	GDREGISTER_CLASS(Button);
	GDREGISTER_CLASS(ColorRect);
	GDREGISTER_CLASS(Panel);
	GDREGISTER_CLASS(TextureRect);
	GDREGISTER_CLASS(ProgressBar);
	GDREGISTER_CLASS(LineEdit);
	GDREGISTER_ABSTRACT_CLASS(Container);
	GDREGISTER_ABSTRACT_CLASS(BoxContainer);
	GDREGISTER_CLASS(VBoxContainer);
	GDREGISTER_CLASS(HBoxContainer);
	GDREGISTER_CLASS(MarginContainer);
	GDREGISTER_CLASS(CenterContainer);
	GDREGISTER_CLASS(GridContainer);
	GDREGISTER_CLASS(ScrollContainer);
	GDREGISTER_CLASS(TabContainer);
	GDREGISTER_CLASS(CheckBox);
	GDREGISTER_CLASS(TextureButton);
	GDREGISTER_ABSTRACT_CLASS(Slider);
	GDREGISTER_CLASS(HSlider);
	GDREGISTER_CLASS(VSlider);
	GDREGISTER_CLASS(OptionButton);
	GDREGISTER_CLASS(ItemList);
	GDREGISTER_CLASS(NinePatchRect);
	GDREGISTER_ABSTRACT_CLASS(Separator);
	GDREGISTER_CLASS(HSeparator);
	GDREGISTER_CLASS(VSeparator);

	// Audio
	GDREGISTER_CLASS(AudioStreamPlayer);
	GDREGISTER_CLASS(AudioStreamPlayer2D);
<<<<<<< HEAD
	GDREGISTER_CLASS(AudioStreamPlayer3D);
	GDREGISTER_CLASS(AudioListener3D);
	GDREGISTER_CLASS(Skeleton3D);
=======
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
	GDREGISTER_ABSTRACT_CLASS(AudioServer);

	ResourceLoader::add_loader(new ResourceFormatLoaderTexture);
	ResourceLoader::add_loader(new ResourceFormatLoaderFont);
	ResourceLoader::add_loader(new ResourceFormatLoaderScene);
	ResourceLoader::add_loader(new ResourceFormatLoaderText);
	ResourceLoader::add_loader(new ResourceFormatLoaderAudio);
	ResourceLoader::add_loader(new ResourceFormatLoaderShader);
	ResourceLoader::add_loader(new ResourceFormatLoaderOBJ);
<<<<<<< HEAD
	ResourceLoader::add_loader(new ResourceFormatLoaderGLTF);
=======
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
}

void unregister_scene_types() {
	SceneTree::flush_delete_queue();
	Font::free_default();
}
