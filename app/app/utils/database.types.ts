// Hand-written to match the live schema (no CLI access). Nullability of the
// alerts columns is unverified; user_settings and notifications are omitted
// because their full column lists could not be confirmed and the app does not
// query them through this client. Enum members were discovered by probing
// candidate values against the live DB, not dumped from the schema, so the
// unions may be incomplete — runtime code must tolerate unlisted values.

export type Json =
  | string
  | number
  | boolean
  | null
  | { [key: string]: Json | undefined }
  | Json[]

export type Database = {
  public: {
    Tables: {
      alerts: {
        Row: {
          created_at: string
          device_id: string | null
          id: number
          location: string | null
          message: string | null
          priority: Database['public']['Enums']['alert_priority'] | null
          status: Database['public']['Enums']['alert_status'] | null
          timestamp: string | null
          type: Database['public']['Enums']['alert_type'] | null
        }
        Insert: {
          created_at?: string
          device_id?: string | null
          id?: number
          location?: string | null
          message?: string | null
          priority?: Database['public']['Enums']['alert_priority'] | null
          status?: Database['public']['Enums']['alert_status'] | null
          timestamp?: string | null
          type?: Database['public']['Enums']['alert_type'] | null
        }
        Update: {
          created_at?: string
          device_id?: string | null
          id?: number
          location?: string | null
          message?: string | null
          priority?: Database['public']['Enums']['alert_priority'] | null
          status?: Database['public']['Enums']['alert_status'] | null
          timestamp?: string | null
          type?: Database['public']['Enums']['alert_type'] | null
        }
        Relationships: []
      }
      tokens: {
        Row: {
          created_at: string
          fcm_token: string
          id: number
          unique_device_id: string
        }
        Insert: {
          created_at?: string
          fcm_token: string
          id?: number
          unique_device_id: string
        }
        Update: {
          created_at?: string
          fcm_token?: string
          id?: number
          unique_device_id?: string
        }
        Relationships: []
      }
    }
    Views: {
      [_ in never]: never
    }
    Functions: {
      [_ in never]: never
    }
    Enums: {
      alert_priority: 'low' | 'medium' | 'high' | 'critical'
      alert_status: 'active' | 'acknowledged' | 'resolved'
      alert_type: 'help' | 'fall' | 'sos' | 'emergency'
    }
    CompositeTypes: {
      [_ in never]: never
    }
  }
}

type DefaultSchema = Database[Extract<keyof Database, 'public'>]

export type Tables<
  TableName extends keyof (DefaultSchema['Tables'] & DefaultSchema['Views']),
> = (DefaultSchema['Tables'] & DefaultSchema['Views'])[TableName] extends {
  Row: infer R
}
  ? R
  : never

export type TablesInsert<TableName extends keyof DefaultSchema['Tables']> =
  DefaultSchema['Tables'][TableName] extends { Insert: infer I } ? I : never

export type TablesUpdate<TableName extends keyof DefaultSchema['Tables']> =
  DefaultSchema['Tables'][TableName] extends { Update: infer U } ? U : never

export type Enums<EnumName extends keyof DefaultSchema['Enums']> =
  DefaultSchema['Enums'][EnumName]
