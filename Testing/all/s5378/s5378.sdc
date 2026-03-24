create_clock -name CK -period 10 -waveform {0 5} [get_ports {CK}]

set_propagated_clock [get_clocks {CK}]

set_input_delay  0 -clock clk [get_ports {n3100gat n3099gat n3098gat n3097gat n3095gat n3094gat n3093gat n3092gat n3091gat n3090gat n3089gat n3088gat n3087gat n3086gat n3085gat n3084gat n3083gat n3082gat n3081gat n3080gat n3079gat n3078gat n3077gat n3076gat n3075gat n3074gat n3073gat n3072gat n3071gat n3070gat n3069gat n3068gat n3067gat n3066gat n3065gat}]
set_output_delay 0 -clock clk [get_ports {n3152gat n3151gat n3150gat n3149gat n3148gat n3147gat n3146gat n3145gat n3144gat n3143gat n3142gat n3141gat n3140gat n3139gat n3138gat n3137gat n3136gat n3135gat n3134gat n3133gat n3132gat n3131gat n3130gat n3129gat n3128gat n3127gat n3126gat n3125gat n3124gat n3123gat n3122gat n3121gat n3120gat n3119gat n3118gat n3117gat n3116gat n3115gat n3114gat n3113gat n3112gat n3111gat n3110gat n3109gat n3108gat n3107gat n3106gat n3105gat n3104gat}]
